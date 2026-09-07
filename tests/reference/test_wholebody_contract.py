import unittest

import numpy as np

from tools.reference.wholebody_contract import semantic_endpoints, validate_simcc


class WholebodyContractTests(unittest.TestCase):
    def test_rejects_coco17_and_mismatched_batch_outputs(self):
        for x, y in (
            (np.zeros((1, 17, 384), np.float32), np.zeros((1, 17, 512), np.float32)),
            (np.zeros((2, 133, 384), np.float32), np.zeros((1, 133, 512), np.float32)),
            (np.zeros((0, 133, 384), np.float32), np.zeros((0, 133, 512), np.float32)),
        ):
            with self.subTest(shape=x.shape), self.assertRaises(ValueError):
                validate_simcc(x, y)

    def test_accepts_wholebody_and_rejects_nonfinite_or_wrong_precision(self):
        x = np.zeros((1, 133, 384), np.float32)
        y = np.zeros((1, 133, 512), np.float32)
        validate_simcc(x, y)
        with self.assertRaises(ValueError):
            validate_simcc(x.astype(np.float64), y)
        y[0, 0, 0] = np.nan
        with self.assertRaises(ValueError):
            validate_simcc(x, y)

    def test_hand_endpoints_use_distinct_actual_landmarks(self):
        points = np.column_stack((np.arange(133), np.arange(133) * 2)).astype(np.float32)
        samples = semantic_endpoints(points, np.ones(133, np.float32), 0.3)
        for name, index in (("ThumbLeft", 95), ("ThumbRight", 116),
                            ("HandtipLeft", 103), ("HandtipRight", 124),
                            ("FootLeft", 17), ("FootRight", 20)):
            self.assertEqual(samples[name]["source_indices"], [index])
            self.assertEqual(samples[name]["pixel"], points[index].tolist())
            self.assertEqual(samples[name]["kind"], "model")
            self.assertTrue(samples[name]["valid"])
        palm = samples["HandLeft"]
        self.assertEqual(palm["kind"], "derived_from_hand_model")
        self.assertEqual(palm["source_indices"], [91, 96, 100, 104, 108])
        np.testing.assert_allclose(palm["pixel"], points[[91, 96, 100, 104, 108]].mean(axis=0))
        self.assertNotEqual(palm["pixel"], points[9].tolist())

    def test_invalid_palm_constituent_does_not_create_a_valid_palm(self):
        points = np.ones((133, 2), np.float32)
        scores = np.ones(133, np.float32)
        scores[100] = 0.1
        samples = semantic_endpoints(points, scores, 0.3)
        self.assertFalse(samples["HandLeft"]["valid"])
        self.assertIsNone(samples["HandLeft"]["pixel"])
        self.assertTrue(samples["HandRight"]["valid"])
        self.assertTrue(samples["ThumbLeft"]["valid"])

    def test_rejects_malformed_decoded_results_and_thresholds(self):
        for points, scores, threshold in (
            (np.ones((17, 2)), np.ones(17), 0.3),
            (np.ones((133, 2)), np.ones(132), 0.3),
            (np.full((133, 2), np.nan), np.ones(133), 0.3),
            (np.ones((133, 2)), np.ones(133), float("nan")),
            (np.ones((133, 2)), np.ones(133), -0.1),
        ):
            with self.subTest(threshold=threshold), self.assertRaises(ValueError):
                semantic_endpoints(points, scores, threshold)


if __name__ == "__main__":
    unittest.main()
