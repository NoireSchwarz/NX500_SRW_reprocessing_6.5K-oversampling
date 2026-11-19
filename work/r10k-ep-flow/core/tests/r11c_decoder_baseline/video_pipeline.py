"""Strict dispatch for preserved R10w and R10x output metadata exports."""
import struct
from video_pipeline1 import DEFINITIONS,active
from video_pipeline1 import decode_pipeline as decode_pipeline1,associate_pipeline as associate_pipeline1
from video_output import decode_output,associate_output

def decode_pipeline(data):
    if len(data)>=8 and struct.unpack_from('<I',data,4)[0]==2:return decode_output(data)
    return decode_pipeline1(data)

def associate_pipeline(pipeline,native):
    if pipeline.get('schema')=='NXL_PIPELINE2':return associate_output(pipeline,native)
    return associate_pipeline1(pipeline,native)
