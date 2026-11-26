"""Lower the pinned OpenCV YuNet graph to the fixed 32x32 INT8 eye engine."""

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path

import numpy as np
import onnx
from onnx import helper, numpy_helper


MODEL_SHA256 = "ebafce4e3c118d6554634be5c27ab333b4c047a9a8c3faf1d7cf93101c22f0f0"
SIDE = 32
HEAD_ORDER = [f"{head}_{stride}" for head in ("cls", "obj", "bbox", "kps") for stride in (8, 16, 32)]


@dataclass
class Stage:
    kind: str
    inputs: tuple
    output: str
    shape: tuple
    weights: np.ndarray = None
    bias: np.ndarray = None
    stride: int = 1
    relu: bool = False


def load_graph(path):
    data = path.read_bytes()
    if sha256(data).hexdigest() != MODEL_SHA256:
        raise ValueError("Expected the pinned OpenCV Zoo YuNet model")
    model = onnx.load_model_from_string(data)
    dims = model.graph.input[0].type.tensor_type.shape.dim
    dims[2].dim_value = dims[3].dim_value = SIDE
    model = onnx.shape_inference.infer_shapes(model, strict_mode=True)
    values = {x.name: x for x in (*model.graph.input, *model.graph.value_info, *model.graph.output)}
    shapes = {name: tuple(d.dim_value for d in x.type.tensor_type.shape.dim) for name, x in values.items()}
    weights = {x.name: numpy_helper.to_array(x) for x in model.graph.initializer}
    producer = {key: node for node in model.graph.node for key in node.output}
    relu_after = {n.input[0]: n.output[0] for n in model.graph.node if n.op_type == "Relu"}
    stages = []
    for node in model.graph.node:
        op = node.op_type
        output = node.output[0]
        if op == "Conv":
            a = {t.name: helper.get_attribute_value(t) for t in node.attribute}
            co, ci, kh, kw = weights[node.input[1]].shape
            assert shapes[node.input[0]][1] == ci * a["group"] and co == shapes[output][1]
            assert a["dilations"] == [1, 1] and kh == kw and a["strides"][0] == a["strides"][1]
            assert a["pads"] == [kh // 2] * 4
            kind = "stem" if node.input[0] == "input" else "pointwise" if kh == 1 else "depthwise"
            assert (kind == "depthwise" and ci == 1 and co == a["group"] and kh == 3 or
                    kind != "depthwise" and a["group"] == 1)
            fused = relu_after.get(output, output)
            stages.append(Stage(kind, (node.input[0],), fused, shapes[fused],
                                weights[node.input[1]], weights[node.input[2]],
                                stride=a["strides"][0], relu=output != fused))
        elif op == "MaxPool":
            a = {t.name: helper.get_attribute_value(t) for t in node.attribute}
            assert a["kernel_shape"] == a["strides"] == [2, 2] and not any(a["pads"])
            stages.append(Stage("maxpool", tuple(node.input), output, shapes[output]))
        elif op == "Add":
            a, b = node.input
            upsample = producer[b]
            attr = {t.name: helper.get_attribute_value(t) for t in upsample.attribute}
            assert upsample.op_type == "Resize" and attr["mode"] == b"nearest" and attr["nearest_mode"] == b"floor"
            assert np.array_equal(weights[upsample.input[2]], [1, 1, 2, 2])
            stages.append(Stage("add_nearest", (a, upsample.input[0]), output, shapes[output]))
        elif op not in {"Resize", "Relu", "Sigmoid", "Transpose", "Reshape"}:
            raise ValueError(f"Unsupported YuNet operation {op}")
    heads = []
    for output in HEAD_ORDER:
        node = producer[output]
        while node.op_type != "Conv":
            assert node.op_type in {"Sigmoid", "Transpose", "Reshape"}
            node = producer[node.input[0]]
        heads.append(node.output[0])
    assert len(stages) == 59
    assert all(last in {s.output for s in stages} for last in heads)
    return model, values, stages, heads


def measure_ranges(model, values, stages, images, percentile):
    import onnxruntime as ort

    patches = np.load(images, allow_pickle=False)
    if patches.ndim != 4 or patches.shape[1:] != (SIDE, SIDE, 3) or patches.dtype != np.uint8:
        raise ValueError("Expected an array of UINT8 NHWC YuNet crops")
    names = [s.output for s in stages if s.kind != "maxpool"]
    del model.graph.output[:]
    model.graph.output.extend(values[name] for name in names)
    options = ort.SessionOptions()
    options.intra_op_num_threads = options.inter_op_num_threads = 1
    session = ort.InferenceSession(model.SerializeToString(), options, providers=["CPUExecutionProvider"])
    collected = [[] for _ in names]
    for patch in patches:
        data = patch.transpose(2, 0, 1)[None].astype(np.float32)
        for target, feature in zip(collected, session.run(names, {"input": data})):
            target.append(feature.flatten())
    ranges = {}
    for name, results in zip(names, collected):
        lo, hi = np.percentile(np.concatenate(results), [percentile, 100 - percentile])
        ranges[name] = [float(min(lo, 0)), float(max(hi, 0))]
    return {"model_sha256": MODEL_SHA256, "patches_sha256": sha256(images.read_bytes()).hexdigest(),
            "patches": len(patches), "tail_percentile": percentile, "ranges": ranges}


def allocate(stages, heads):
    last_use = {key: len(stages) for key in heads}
    for i, stage in enumerate(stages):
        for key in stage.inputs:
            last_use[key] = max(last_use.get(key, i), i)
    high, active, offsets = 0, {}, {}
    for i, stage in enumerate(stages):
        active = {key: region for key, region in active.items() if last_use[key] >= i}
        size = (int(np.prod(stage.shape)) + 15) & ~15
        offset = 0
        for lo, hi in sorted(active.values()):
            if offset + size <= lo:
                break
            offset = max(offset, hi)
        key = stage.output
        offsets[key], active[key] = offset, (offset, offset + size)
        high = max(high, offset + size)
    return offsets, high


def emit_array(lines, kind, name, values, width=24):
    lines.append(f"_Alignas(16) static const {kind} {name}[] = {{")
    for row in range(0, len(values), width):
        lines.append("    " + ", ".join(map(str, values[row:row + width])) + ",")
    lines.append("};")


def c_float(x):
    s = f"{x:.9g}"
    return s + ("f" if any(c in s for c in ".e") else ".0f")


def affine(bounds, min_scale=1e-9):
    lo, hi = bounds
    scale = max((hi - lo) / 255, min_scale)
    return scale, int(np.clip(round(-128 - lo / scale), -128, 127))


def generate(stages, heads, calibration):
    if calibration["model_sha256"] != MODEL_SHA256:
        raise ValueError("Calibration belongs to another model")
    offsets, arena = allocate(stages, heads)
    params, calls, definitions, macs = {"input": (1., -128)}, [], [], 0
    shapes = {"input": (1, 3, SIDE, SIDE), **{s.output: s.shape for s in stages}}
    ptr = lambda key: "input" if key == "input" else f"arena + {offsets[key]}"
    for index, stage in enumerate(stages):
        source = stage.inputs[0]
        sc, zp = params[source]
        _, ci, h, w = shapes[source]
        _, co, oh, ow = stage.shape
        assert h == w and oh == ow
        take, put = ptr(source), ptr(stage.output)
        if stage.kind == "maxpool":
            params[stage.output] = sc, zp
            calls.append(f"maxpool({take}, {put}, {w}, {ci});")
            continue
        limits = calibration["ranges"][stage.output]
        if stage.kind == "add_nearest":
            other = stage.inputs[1]
            so, zo = affine(limits)
            sb, zb = params[other]
            a, b = round(sc / so * 32768), round(sb / so * 32768)
            offset = zo * 32768 - zp * a - zb * b
            assert abs(offset) + (abs(a) + abs(b)) * 128 < (1 << 31) - 16384
            params[stage.output] = so, zo
            calls.append(f"add_nearest({take}, {ptr(other)}, {put}, {w}, {ci}, {a}, {b}, {offset});")
            continue
        weight = stage.weights
        if stage.kind == "stem":
            weight = weight.transpose(0, 2, 3, 1)
        weight = weight.astype(np.float64).reshape(co, -1)
        ws = np.maximum(np.max(np.abs(weight), axis=1) / 127, 1e-10)
        integers = np.rint(weight / ws[:, None]).clip(-127, 127).astype(np.int8)
        so, zo = affine(limits, float(max(ws) * sc * 1.000001))
        params[stage.output] = so, zo
        multipliers = np.rint(ws * sc / so * (1 << 31)).astype(np.int64)
        bias = np.rint(stage.bias / (ws * sc)).astype(np.int64) - zp * integers.astype(np.int64).sum(axis=1)
        assert max(abs(bias)) + integers.size * 128 * 127 < 1 << 31
        assert min(multipliers) > 0 and max(multipliers) < 1 << 31
        macs += oh * ow * integers.size
        if stage.kind == "stem":
            assert (w, ow, ci, co, stage.stride) == (32, 16, 3, 16, 2)
            integers = np.pad(integers, ((0, 0), (0, 5)))
            args = f"{take}, {put}"
        elif stage.kind == "depthwise":
            assert stage.stride == 1 and ci == co
            integers = integers.T
            args = f"{take}, {put}, {w}, {ci}"
        else:
            assert stage.stride == 1 and ci % 16 == 0
            args = f"{take}, {put}, {w * h}, {ci}, {co}"
        emit_array(definitions, "int8_t", f"w{index}", integers.flatten().tolist())
        emit_array(definitions, "int32_t", f"b{index}", bias.tolist(), 10)
        emit_array(definitions, "int32_t", f"m{index}", multipliers.tolist(), 10)
        definitions.append(f"static const struct quant_conv q{index} = "
                           f"{{w{index}, b{index}, m{index}, {zp}, {zo}, {zo if stage.relu else -128}}};")
        calls.append(f"{stage.kind}({args}, &q{index});")
    assert macs == 860560
    definitions.append(f"enum {{ YUNET_ARENA_BYTES = {arena}, YUNET_MACS = {macs} }};")
    definitions.append("static const struct head_scale head_scales[] = {")
    definitions.extend(f"    {{{c_float(params[h][0])}, {params[h][1]}}}," for h in heads)
    definitions.append("};")
    def lut(key, name, sigmoid):
        scale, zero = params[key]
        values = (np.arange(-128, 128) - zero) * scale
        if sigmoid:
            p = 1 / (1 + np.exp(-np.clip(values, -24, 24)))
            emit_array(definitions, "uint16_t", name, np.rint(p * 65535).astype(int).tolist(), 14)
        else:
            p = np.exp(np.clip(values, -12, 8))
            emit_array(definitions, "float", name, [c_float(v) for v in p], 8)
    for i, h in enumerate(heads[:6]):
        lut(h, f"score{i}", True)
    for i, h in enumerate(heads[6:9]):
        lut(h, f"extent{i}", False)
    definitions.append("static const uint16_t *const scores[] = {score0, score1, score2, score3, score4, score5};")
    definitions.append("static const float *const extents[] = {extent0, extent1, extent2};")
    definitions.extend(["", "static void yunet_forward(const int8_t *input, int8_t *arena, const int8_t *out[12])", "{"])
    definitions.extend("    " + s for s in calls)
    definitions.extend(f"    out[{i}] = {ptr(h)};" for i, h in enumerate(heads))
    definitions.extend(["}", ""])
    return "/* Generated from pinned YuNet weights. See model/YUNET-LICENSE.txt. */\n" + "\n".join(definitions)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    parser.add_argument("calibration", type=Path)
    parser.add_argument("header", type=Path)
    parser.add_argument("--calibrate", type=Path)
    parser.add_argument("--tail-percentile", type=float, default=0.0)
    args = parser.parse_args()
    graph, values, nodes, outputs = load_graph(args.model)
    if args.calibrate:
        calibration = measure_ranges(graph, values, nodes, args.calibrate, args.tail_percentile)
        args.calibration.write_text(json.dumps(calibration, indent=2) + "\n", encoding="ascii")
    else:
        calibration = json.loads(args.calibration.read_text("ascii"))
    args.header.write_text(generate(nodes, outputs, calibration), encoding="ascii", newline="\n")
