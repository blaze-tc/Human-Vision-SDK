"""Check Unity's real cameraImage.png capture against four annotated front-row regions.

Regions are acceptance annotations only; the inference pipeline never reads them.
This verifies coverage and valid COCO-17 output, not precise pose accuracy or FPS.
"""
import argparse
import json
from pathlib import Path


def verify(path):
    record = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    assert record['sequence'] > 0, 'No completed inference'
    assert record['sourceFrame'] == record['presentationFrame'], 'Video/pose frame mismatch'
    assert not record['error'] and record['readbackErrors'] == 0
    width, height = record['width'], record['height']
    selected = []
    for left, right in ((.06, .17), (.29, .40), (.59, .72), (.82, .94)):
        matches = []
        for body in record['bodies']:
            box = body['box']
            cx = (box['x'] + box['width'] / 2) / width
            bottom = (box['y'] + box['height']) / height
            if left <= cx <= right and bottom >= .84 and box['height'] / height >= .34:
                matches.append(body)
        assert len(matches) == 1, f'Expected one front-row person in {left, right}, got {len(matches)}'
        body = matches[0]
        assert len(body['joints']) == 17 and all(j['valid'] for j in body['joints']), 'Incomplete COCO-17 pose'
        selected.append(body['id'])
    assert len(set(selected)) == 4
    return selected


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    args = parser.parse_args()
    print('PASS: four front-row skeletons, 17 valid joints each; track IDs:', verify(args.evidence))
