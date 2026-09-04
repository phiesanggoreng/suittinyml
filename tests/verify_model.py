"""Uji artefak yang benar-benar dipakai firmware; jalankan dari root proyek."""
import os
os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
import csv
import json
import re
from pathlib import Path
import numpy as np
import tensorflow as tf

root = Path(__file__).resolve().parents[1]
binary = (root / "models/suit_float32.tflite").read_bytes()
header = (root / "wokwi/model_data.h").read_text(encoding="utf-8")
assert bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", header)) == binary
interpreter = tf.lite.Interpreter(model_content=binary, num_threads=1)
interpreter.allocate_tensors()
inp, out = interpreter.get_input_details()[0], interpreter.get_output_details()[0]
assert inp["shape"].tolist() == [1, 3]
assert out["shape"].tolist() == [1, 4]
assert inp["dtype"] == out["dtype"] == np.float32

checks = []
for x, expected in [([0,0,0],0),([1,1,0],1),([1,1,1],2),
                    ([0,0,1],3),([0,1,0],3),([0,1,1],3),
                    ([1,0,0],3),([1,0,1],3),([.5,.5,.5],3)]:
    interpreter.set_tensor(inp["index"], np.array([x], dtype=np.float32))
    interpreter.invoke()
    scores = interpreter.get_tensor(out["index"])[0]
    label = int(scores.argmax())
    assert label == expected, (x, scores)
    if expected != 3:
        assert scores.max() >= .75, (x, scores)
    checks.append({"input_normalized": x, "expected": expected,
                   "predicted": label, "score": float(scores.max())})

# Pastikan setiap split tidak berbagi tuple fitur yang sama.
with (root / "data/dataset_sintetis.csv").open(encoding="utf-8", newline="") as dataset:
    rows = list(csv.DictReader(dataset))
sets = {s: {tuple(int(r[k]) for k in ["input_1", "input_2", "input_3"])
            for r in rows if r["split"] == s} for s in ["train", "validation", "test"]}
assert not sets["train"] & sets["validation"]
assert not sets["train"] & sets["test"]
assert not sets["validation"] & sets["test"]
report = {"header_matches_tflite": True, "tensor_shapes_types_valid": True,
          "split_feature_tuples_disjoint": True, "canonical_pose_tests": checks}
reports = root / "reports"
reports.mkdir(parents=True, exist_ok=True)
(reports / "model_checks.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report, indent=2))
