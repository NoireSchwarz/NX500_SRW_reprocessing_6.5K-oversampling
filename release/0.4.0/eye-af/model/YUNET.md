# YuNet model provenance

`yunet-2026may.onnx` is the unmodified `face_detection_yunet_2026may.onnx` from OpenCV Zoo, pinned to commit `47534e27c9851bb1128ccc0102f1145e27f23f98`. The file is 229,738 bytes. `pack_yunet.py` checks its SHA-256 before converting the graph.

```
ebafce4e3c118d6554634be5c27ab333b4c047a9a8c3faf1d7cf93101c22f0f0
```

The NX500 port uses the eye landmarks from a 32 × 32 crop of the face selected by the camera. The ARM engine uses INT8 weights and intermediate tensors generated at build time from the pinned graph and `yunet-calibration.json`. The bundled weights and generated parameters carry the [YuNet MIT notice](YUNET-LICENSE.txt), copyright 2020 Shiqi Yu. The NX500 integration sources are AGPL-3.0-only.

[Pinned model and Git LFS checksum](https://github.com/opencv/opencv_zoo/blob/47534e27c9851bb1128ccc0102f1145e27f23f98/models/face_detection_yunet/face_detection_yunet_2026may.onnx) · [Upstream MIT license](https://github.com/opencv/opencv_zoo/blob/47534e27c9851bb1128ccc0102f1145e27f23f98/models/face_detection_yunet/LICENSE)

Wei Wu, Hanyang Peng and Shiqi Yu. *YuNet: A Tiny Millisecond-level Face Detector*. Machine Intelligence Research 20, 656–665 (2023). [DOI 10.1007/s11633-023-1423-y](https://doi.org/10.1007/s11633-023-1423-y)
