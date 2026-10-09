"""将固定 ONNX 转成 RK3588 候选模型；转换成功不等于精度/设备验收。

需要 Linux + RKNN-Toolkit2 2.3.2 + ONNX。此工具不修改已发布的 NCNN
ModelPack，不生成可部署 profile，不设置 SDK 的后端可用标志。
"""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import re

TOOLKIT_VERSION = '2.3.2'


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def validate_source(model, expected):
    if not re.fullmatch('[a-f0-9]{64}', expected) or sha256(model) != expected:
        raise ValueError('Source ONNX SHA-256 mismatch; refuse unpinned conversion')


def validate_calibration(precision, dataset):
    if precision == 'non-quantized':
        return None
    if precision != 'int8' or dataset is None:
        raise ValueError('INT8 requires a real calibration image list')
    dataset = Path(dataset).resolve()
    if not dataset.is_file():
        raise ValueError('Missing calibration image list')
    images = []
    for line in dataset.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        path = Path(line.strip())
        if not path.is_absolute():
            path = dataset.parent / path
        path = path.resolve()
        # 单输入图片列表；禁止误把带空白的路径分成多个模型输入。
        if not path.is_file():
            raise ValueError('calibration image missing: ' + str(path))
        if any(character.isspace() for character in str(path)):
            raise ValueError('calibration paths must not contain whitespace')
        images.append(path)
    if not images:
        raise ValueError('Empty calibration image list')
    return images


def candidate_receipt(source_hash, model_hash, precision, toolkit):
    return dict(target='rk3588', toolkit_version=toolkit,
                source_onnx_sha256=source_hash, rknn_sha256=model_hash,
                precision_request=precision, deployment_ready=False,
                numerical_gate_passed=False, device_performance_verified=False,
                note='Conversion artifact only; actual tensor precision, output schema, accuracy, '
                     'runtime/driver compatibility and end-to-end performance remain unverified.')


def checked(stage, result):
    if result != 0:
        raise RuntimeError(f'RKNN {stage} failed with status {result}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--onnx', required=True, type=Path)
    parser.add_argument('--source-sha256', required=True)
    parser.add_argument('--output', required=True, type=Path, help='New candidate directory')
    parser.add_argument('--precision', choices=('non-quantized', 'int8'), default='non-quantized')
    parser.add_argument('--calibration', type=Path)
    parser.add_argument('--validation-index', type=Path, help='Pinned Low fixture bank; ORT and x86 simulator gates')
    parser.add_argument('--validation-index-sha256')
    args = parser.parse_args()
    if bool(args.validation_index) != bool(args.validation_index_sha256):
        raise ValueError('Validation index and its SHA-256 must be supplied together')
    validate_source(args.onnx, args.source_sha256)
    images = validate_calibration(args.precision, args.calibration)
    if args.output.exists():
        raise ValueError('Output directory must be new; preserve prior model/evidence')
    if platform.system() != 'Linux':
        raise RuntimeError('RKNN conversion requires Linux; use a working Linux host/WSL2/container')
    version = importlib.metadata.version('rknn-toolkit2')
    if version.split('+')[0] != TOOLKIT_VERSION:
        raise RuntimeError('Expected pinned RKNN-Toolkit2 ' + TOOLKIT_VERSION + ', found ' + version)
    import onnx
    from rknn.api import RKNN
    graph = onnx.load(str(args.onnx)).graph
    initializer_names = {value.name for value in graph.initializer}
    inputs = [value for value in graph.input if value.name not in initializer_names]
    def shape(value):
        return [dimension.dim_value if dimension.HasField('dim_value') else dimension.dim_param
                for dimension in value.type.tensor_type.shape.dim]
    if len(inputs) != 1:
        raise ValueError('Pose candidate must have exactly one static RGB NCHW input')
    dimensions = shape(inputs[0])
    if (len(dimensions) != 4 or dimensions[:2] != [1, 3] or
            any(not isinstance(n, int) or n <= 0 or n > 960 or n % 32 for n in dimensions[2:])):
        raise ValueError('Expected static [1,3,H,W], positive H/W <=960 and multiples of32')
    args.output.mkdir(parents=True)
    if args.validation_index:
        from simulator_gate import onnx_gate
        reference = onnx_gate(args.onnx, args.validation_index, args.validation_index_sha256,
                              args.output / 'onnx-reference')
        if not reference['offline_numerical_passed']:
            raise ValueError('Recovered ONNX failed unchanged NCNN numerical gates')
    output = args.output / 'candidate.rknn'
    dataset = None
    calibration = []
    if images is not None:
        dataset = args.output / 'calibration-resolved.txt'
        dataset.write_text('\n'.join(str(path) for path in images) + '\n', encoding='utf-8')
        calibration = [dict(file=str(path), sha256=sha256(path)) for path in images]
    rknn = RKNN(verbose=True)
    try:
        # 图片以 RGB uint8 输入，/255 融入转换合同；后端不能再次执行 /255。
        checked('config', rknn.config(mean_values=[[0, 0, 0]], std_values=[[255, 255, 255]], target_platform='rk3588'))
        checked('load_onnx', rknn.load_onnx(model=str(args.onnx.resolve())))
        checked('build', rknn.build(do_quantization=images is not None,
                                   dataset=str(dataset.resolve()) if dataset else None))
        checked('export', rknn.export_rknn(str(output.resolve())))
        if not output.is_file() or output.stat().st_size == 0:
            raise RuntimeError('RKNN returned success without a nonempty model')
        receipt = candidate_receipt(args.source_sha256, sha256(output), args.precision, version)
        receipt.update(input=dict(name=inputs[0].name, shape=dimensions, color_order='RGB',
                                  mean=[0, 0, 0], std=[255, 255, 255]),
                       source_outputs=[dict(name=value.name, shape=shape(value)) for value in graph.output],
                       calibration=calibration)
        (args.output / 'conversion-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        if args.validation_index:
            from simulator_gate import rknn_gate
            comparison = rknn_gate(rknn, args.validation_index, args.validation_index_sha256,
                                   args.output / 'simulator', receipt['rknn_sha256'])
            if not comparison['offline_numerical_passed']:
                raise ValueError('RKNN simulator failed unchanged NCNN numerical gates; candidate is not qualified')
        print('Candidate converted. deployment_ready=false; run independent numerical and RK3588 device gates.')
    finally:
        rknn.release()


if __name__ == '__main__':
    main()
