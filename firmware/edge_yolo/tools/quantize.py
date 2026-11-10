"""Reproducible small-input ESP32-S3 export using the pinned ESP-DL tutorial.

COCO8 is a bring-up calibration set, not an accuracy validation set for height.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import functools

ROOT = Path(__file__).resolve().parents[3]
WORKSPACE_ROOT = ROOT.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size", type=int, default=224, choices=[160, 192, 224, 256])
    parser.add_argument("--tqt-steps", type=int, default=8)
    local_model = ROOT / "yolo26n.pt"
    repository_model = ROOT.parent / "yolo26n.pt"
    sibling_model = ROOT.parent / "ESP32Camera-OhStem-AI/yolo26n.pt"
    if local_model.is_file():
        default_model = local_model
    elif repository_model.is_file():
        default_model = repository_model
    else:
        default_model = sibling_model
    parser.add_argument("--model", type=Path, default=default_model)
    args = parser.parse_args()
    shared_source = WORKSPACE_ROOT / ".edge-deps/esp-dl/examples/tutorial/how_to_quantize_model/quantize_yolo26"
    source = shared_source if (shared_source / "quantize_yolo26_coco.ipynb").exists() else ROOT / ".edge-deps/quantize_yolo26"
    if not (source / "quantize_yolo26_coco.ipynb").exists():
        parser.error("Run prepare_sources.ps1 first")
    config_dir = ROOT / ".edge-deps/ultralytics-config"
    config_dir.mkdir(parents=True, exist_ok=True)
    os.environ["YOLO_CONFIG_DIR"] = str(config_dir)
    os.environ["MPLBACKEND"] = "Agg"
    mpl_dir = ROOT / ".edge-deps/matplotlib-config"
    mpl_dir.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str(mpl_dir)
    import torch
    import yaml
    from ultralytics import YOLO
    torch.set_num_threads(4)
    model = YOLO(str(args.model.resolve()))
    shared_dataset = WORKSPACE_ROOT / ".edge-deps/datasets/coco8"
    dataset = shared_dataset if (shared_dataset / "images/train").is_dir() else ROOT / ".edge-deps/datasets/coco8"
    if not (dataset / "images/train").is_dir():
        parser.error("COCO8 calibration images missing (run prepare_sources.ps1)")
    dataset_yaml = ROOT / ".edge-deps/edge_coco8.yaml"
    dataset_yaml.write_text(yaml.safe_dump({"path": str(dataset.resolve()),
        "train": "images/train", "val": "images/val", "names": model.names}), encoding="utf-8")
    del model

    cells = json.loads((source / "quantize_yolo26_coco.ipynb").read_text(encoding="utf-8"))["cells"]
    os.chdir(source)
    sys.path.insert(0, str(source))
    sys.path.insert(0, str(source / "scripts"))
    namespace = {"__name__": "edge_quantization"}

    def execute(index):
        print(f"[QUANTIZE] Official cell {index}", flush=True)
        exec(compile("".join(cells[index]["source"]), f"official_cell_{index}", "exec"), namespace)

    execute(1)
    # The notebook assumes CUDA; this setup deliberately uses the CPU environment.
    namespace["TorchExecutor"] = functools.partial(namespace["TorchExecutor"], device="cpu")
    namespace.update(IMG_SZ_I=args.size, PLATFORM="s3", DATA_YAML_FILE_I=str(dataset_yaml), INT16_LUT_STEP_I=32)
    execute(3)
    config = namespace["QATConfig"]
    config.DEVICE = "cpu"
    config.BATCH_SIZE = 1
    config.CALIB_MAX_IMAGES = 8
    config.CALIB_STEPS = 8
    config.TQT_STEPS = args.tqt_steps
    config.PT_FILE = str(args.model.resolve())
    config.ESPDL_OUTPUT_DIR = str(ROOT / "firmware/edge_yolo/models")
    config.ONNX_PATH = str(Path(config.ESPDL_OUTPUT_DIR) / f"yolo26n_{args.size}_export.onnx")
    execute(4)
    # The upstream exporter otherwise inherits Ultralytics' batch=16 default.
    # A microcontroller must have a fixed batch of one.
    def export_batch_one():
        from export import ESP_YOLO, apply_export_patches
        model = ESP_YOLO(config.PT_FILE)
        apply_export_patches(model)
        model.export(format="onnx", opset=13, simplify=True, imgsz=config.IMG_SZ,
                     batch=1, dynamic=False)
    namespace["prepare_onnx"] = export_batch_one
    for index in (5, 6, 7, 8, 10, 12):
        execute(index)
    output = Path(namespace["final_espdl_path"])
    rev_file = source / "UPSTREAM_REVISION.txt"
    source_rev = rev_file.read_text().strip() if rev_file.exists() else "512ed0b0dc5f5320f3d9ca5a5aea189803f87f31"
    record = {"source_revision": source_rev,
        "input_size": args.size, "target": "esp32s3", "format": "ESP-DL",
        "calibration_dataset": "COCO8 train", "calibration_steps": 8,
        "tqt_steps": args.tqt_steps, "bytes": output.stat().st_size,
        "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "accuracy_validated": False}
    output.with_suffix(".build.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    print(f"[QUANTIZE] Complete: {output} ({record['bytes']} bytes)", flush=True)


if __name__ == "__main__":
    main()
