import copy
import unittest

from tools.test.analyze_android_r4_model_outputs import associate_poses


class R4PoseAssociationTests(unittest.TestCase):
    def setUp(self):
        self.body = {'frame': 7, 'timestamp': 100, 'joints': [
            {'index': index, 'valid': True, 'x': x, 'y': y,
             'confidence': .8, 'frame': 7, 'timestamp': 100}
            for index, x, y in [(5, 20, 20), (12, 40, 20),
                                 (18, 20, 50), (22, 40, 50), (0, 30, 50)]]}
        self.regions = {'people': [{'id': 'person',
            'shoulder_midpoint_xyxy': [25, 15, 35, 25],
            'pelvis_midpoint_xyxy': [25, 45, 35, 55]}]}

    def test_independent_midpoints_associate(self):
        result = associate_poses({0: self.body}, self.regions, 100, 100)
        self.assertEqual(result[0]['annotation'], 'person')
        self.assertEqual(result[0]['valid_canonical_joints'], 5)

    def test_ambiguous_people_fail(self):
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            associate_poses({0: self.body, 1: copy.deepcopy(self.body)}, self.regions, 100, 100)

    def test_stale_joint_fails(self):
        self.body['joints'][0]['timestamp'] = 99
        with self.assertRaisesRegex(ValueError, 'stale'):
            associate_poses({0: self.body}, self.regions, 100, 100)

    def test_unique_shoulder_can_associate_when_pelvis_region_misses(self):
        self.body['joints'][2]['y'] = 80
        self.body['joints'][3]['y'] = 80
        result = associate_poses({0: self.body}, self.regions, 100, 100)
        self.assertEqual(result[0]['matched_regions'], ['shoulder'])
        self.assertEqual(result[0]['unmatched_regions'], ['pelvis'])

    def test_neither_anatomical_region_matches(self):
        for joint in self.body['joints']:
            joint['x'] = 90
        with self.assertRaisesRegex(ValueError, 'missing'):
            associate_poses({0: self.body}, self.regions, 100, 100)


if __name__ == '__main__':
    unittest.main()
