"""Fold terminal Concat(Transpose(...)) into Transpose(Concat(...)).

Weights, logical output order and arithmetic are unchanged. This is a separate
candidate: fewer ONNX transposes do not prove fewer compiled operations or FPS.
"""
import argparse
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import onnx
from onnx import helper


def optimize(source):
    model = copy.deepcopy(source)
    graph = model.graph
    producers = {name: (i, node) for i, node in enumerate(graph.node) for name in node.output}
    consumers = Counter(name for node in graph.node for name in node.input)
    outputs = {o.name for o in graph.output}
    names = set(producers) | {v.name for v in graph.input} | {v.name for v in graph.initializer}
    removed, replacements = set(), {}
    for output in graph.output:
        if output.name not in producers:
            continue
        index, cat = producers[output.name]
        attrs = {a.name: helper.get_attribute_value(a) for a in cat.attribute}
        if cat.op_type != 'Concat' or cat.domain or attrs.get('axis') != 1 or len(cat.input) < 2:
            continue
        parents = [producers.get(name) for name in cat.input]
        if any(p is None for p in parents):
            continue
        if any(n.op_type != 'Transpose' or n.domain or len(n.input) != 1 or
               len(n.output) != 1 or n.output[0] in outputs or consumers[n.output[0]] != 1 or
               {a.name: helper.get_attribute_value(a) for a in n.attribute}.get('perm') != [0, 2, 1]
               for _, n in parents):
            continue
        name = output.name + '_before_transpose'
        if name in names:
            raise ValueError('Generated tensor name collision')
        names.add(name)
        replacements[index] = [
            helper.make_node('Concat', [n.input[0] for _, n in parents], [name], axis=2),
            helper.make_node('Transpose', [name], [output.name], perm=[0, 2, 1])]
        removed.update(i for i, _ in parents)
    if not replacements:
        raise ValueError('No eligible isolated terminal transpose/concat branches')
    nodes = []
    for i, node in enumerate(graph.node):
        if i not in removed:
            nodes.extend(replacements.get(i, [node]))
    del graph.node[:]
    graph.node.extend(nodes)
    onnx.checker.check_model(model)
    return model, len(replacements)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--source-sha256', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    raw = args.source.read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    if actual != args.source_sha256:
        raise ValueError('Source SHA256 mismatch')
    if args.output.exists() or args.output.with_suffix('.receipt.json').exists():
        raise ValueError('Output must be new; preserve previous candidate')
    source = onnx.load_model_from_string(raw)
    if any(t.data_location == onnx.TensorProto.EXTERNAL for t in source.graph.initializer):
        raise ValueError('External weights are not covered by the source hash')
    result, changed = optimize(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    encoded = result.SerializeToString()
    args.output.write_bytes(encoded)
    receipt = dict(source_sha256=actual, candidate_sha256=hashlib.sha256(encoded).hexdigest(),
                   terminal_groups_changed=changed, weights_unchanged=True,
                   deployment_ready=False, performance_verified=False)
    args.output.with_suffix('.receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
