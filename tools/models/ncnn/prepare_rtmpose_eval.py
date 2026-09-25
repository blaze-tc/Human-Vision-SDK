"""Reproduce the single approved local Body26 ncnn candidate, without ORT.

The exact MMDeploy graph contains custom pooling and symbolic output metadata.
It is eligible only through its pinned fixed-shape rewrite and device golden;
the generic C1 ONNX audit remains strict and unchanged.
"""
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

import onnx
from onnx import helper

from tools.models.ncnn.model_contract import (activate_pinned_vendor,require_hash,sha256_file,
    canonical_json,POSE_CHECKPOINT_SHA256,PINNED_REVISIONS)
from tools.models.ncnn.pad_rtmpose_first_conv import pad_first_conv, SOURCE_SHA256
from tools.models.ncnn.fuse_rtmpose_first_norm import fuse
from tools.models.ncnn.finalize_rtmpose_vulkan_shapes import finalize
from tools.models.ncnn.build_local_eval_pack import (POSE_ONNX_SHA256, POSE_PARAM_SHA256,
    POSE_BIN_SHA256,CONVERTER_SHA256,OPTIMIZER_SHA256)

FIXTURE_SHA256='7a090e3befceef2fe0db7e8b8a2a2ec03782e8e133a4738c027e3f6f23afac31'


def verify_static_body26(path: Path) -> dict:
    require_hash(path,POSE_ONNX_SHA256,'fused official Body26 ONNX')
    model=onnx.load(path,load_external_data=False)
    onnx.checker.check_model(model)
    if list(model.graph.input[0].type.tensor_type.shape.dim)[1].dim_value != 4:
        raise ValueError('Expected fixed four-channel zero-padded input')
    nodes=list(model.graph.node)
    initializers={t.name:t for t in model.graph.initializer}
    result={}
    for axis,length in (('x',384),('y',512)):
        node=next(n for n in nodes if n.name==f'/cls_{axis}/Gemm')
        attrs={a.name:helper.get_attribute_value(a) for a in node.attribute}
        if (node.op_type!='Gemm' or list(node.input)!=['/gau/Add_2_output_0','B.7' if axis=='x' else 'B']
                or list(node.output)!=[f'simcc_{axis}'] or attrs!={'alpha':1.,'beta':1.,'transA':0,'transB':1}
                or list(initializers[node.input[1]].dims)!=[length,256]):
            raise ValueError('Pinned terminal classifier contract mismatch')
        # The input is fixed batch1, 26 channels after final Conv, flattened
        # to 26 rows; the pinned GAU preserves those rows. Device runner also
        # rejects every output not exactly 26*length FP32 values.
        result[f'simcc_{axis}']={'shape':[1,26,length],'download_dtype':'fp32','max_bytes':26*length*4}
    return result


def prepare(source_onnx: Path, converter: Path, optimizer: Path, output: Path) -> dict:
    for path,digest,label in ((source_onnx,SOURCE_SHA256,'official ONNX'),(converter,CONVERTER_SHA256,'MMDeploy converter'),(optimizer,OPTIMIZER_SHA256,'ncnn optimizer')):
        require_hash(path,digest,label)
    output.mkdir(parents=True,exist_ok=True)
    pad=pad_first_conv(source_onnx,output/'padded.onnx')
    fusion=fuse(output/'padded.onnx',output/'model.onnx')
    outputs=verify_static_body26(output/'model.onnx')
    commands=[[str(converter),str(output/'model.onnx'),str(output/'mmdeploy.param'),str(output/'mmdeploy.bin')],
              [str(optimizer),str(output/'mmdeploy.param'),str(output/'mmdeploy.bin'),str(output/'model.param'),str(output/'model.bin'),'65536']]
    for command,name in zip(commands,('convert','optimize')):
        with (output/(name+'.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    shapes=finalize(output/'model.param',output/'vulkan.param')
    require_hash(output/'vulkan.param',POSE_PARAM_SHA256,'eligible Vulkan graph')
    require_hash(output/'model.bin',POSE_BIN_SHA256,'eligible FP16 weights')
    result={'local_evaluation_only':True,'source_revisions':PINNED_REVISIONS,'source_onnx_sha256':SOURCE_SHA256,
        'padding':pad,'first_norm_fusion':fusion,'shape_finalizer':shapes,'output_contract':outputs,
        'commands':commands,'onnx2ncnn_sha256':CONVERTER_SHA256,'ncnnoptimize_sha256':OPTIMIZER_SHA256,
        'param_sha256':POSE_PARAM_SHA256,'bin_sha256':POSE_BIN_SHA256,
        'warning':'Eligibility still requires all four device golden cases. Trained-weight redistribution unverified.'}
    (output/'conversion.json').write_text(canonical_json(result),encoding='utf-8',newline='\n')
    return result


def export_official(checkpoint: Path,vendor_root: Path,image: Path,output: Path) -> None:
    require_hash(checkpoint,POSE_CHECKPOINT_SHA256,'Body26 checkpoint')
    require_hash(image,FIXTURE_SHA256,'official source fixture')
    activate_pinned_vendor(vendor_root)
    from mmengine import Config
    from mmdeploy.apis import torch2onnx
    config=vendor_root/'mmpose/projects/rtmpose/rtmpose/body_2d_keypoint/rtmpose-t_8xb1024-700e_body8-halpe26-256x192.py'
    deploy=Config.fromfile(str(vendor_root/'mmdeploy/configs/mmpose/pose-detection_simcc_ncnn-fp16_static-256x192.py'))
    deploy.onnx_config.input_names=['in0'];output.parent.mkdir(parents=True,exist_ok=True)
    deploy_path=output.with_suffix('.deploy.py');deploy.dump(str(deploy_path))
    torch2onnx(str(image.resolve()),str(output.parent.resolve()),output.name,str(deploy_path.resolve()),str(config.resolve()),str(checkpoint.resolve()),'cpu')
    require_hash(output,SOURCE_SHA256,'reproduced official ONNX')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-onnx',type=Path,required=True)
    parser.add_argument('--converter',type=Path,required=True)
    parser.add_argument('--optimizer',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--export',action='store_true',help='Regenerate source ONNX using the three required export inputs')
    parser.add_argument('--checkpoint',type=Path)
    parser.add_argument('--vendor-root',type=Path)
    parser.add_argument('--image',type=Path)
    args=parser.parse_args()
    if args.export:
        if any(v is None for v in (args.checkpoint,args.vendor_root,args.image)):parser.error('--export requires --checkpoint --vendor-root --image')
        export_official(args.checkpoint,args.vendor_root,args.image,args.source_onnx)
    print(canonical_json(prepare(args.source_onnx,args.converter,args.optimizer,args.output)))
