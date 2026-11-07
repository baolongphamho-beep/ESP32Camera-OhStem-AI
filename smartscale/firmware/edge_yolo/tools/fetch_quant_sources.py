"""Fetch the pinned official quantizer scripts, excluding generated models/logs."""
import concurrent.futures
import json
from pathlib import Path
import requests

ROOT = Path(__file__).resolve().parents[3]
REVISION = "512ed0b0dc5f5320f3d9ca5a5aea189803f87f31"
PREFIX = "examples/tutorial/how_to_quantize_model/quantize_yolo26/"


def main():
    cache = ROOT / ".edge-deps"
    cache.mkdir(exist_ok=True)
    tree_path = cache / "espdl-tree.json"
    if tree_path.exists():
        tree = json.loads(tree_path.read_text(encoding="utf-8"))
    else:
        response = requests.get(f"https://api.github.com/repos/espressif/esp-dl/git/trees/{REVISION}?recursive=1", timeout=60)
        response.raise_for_status()
        tree = response.json()
        tree_path.write_text(json.dumps(tree), encoding="utf-8")
    destination = (cache / "quantize_yolo26").resolve()
    paths = [item["path"] for item in tree["tree"] if item["type"] == "blob" and
             item["path"].startswith(PREFIX) and "/output/" not in item["path"] and
             Path(item["path"]).suffix in {".py", ".txt", ".ipynb", ".yaml", ".json"}]

    def fetch(path):
        target = (destination / path.removeprefix(PREFIX)).resolve()
        if not target.is_relative_to(destination):
            raise ValueError("Invalid source path")
        target.parent.mkdir(parents=True, exist_ok=True)
        response = requests.get(f"https://raw.githubusercontent.com/espressif/esp-dl/{REVISION}/{path}", timeout=60)
        response.raise_for_status()
        target.write_bytes(response.content)
        return target.name

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for name in pool.map(fetch, paths):
            print(f"Downloaded {name}", flush=True)
    (destination / "UPSTREAM_REVISION.txt").write_text(REVISION + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
