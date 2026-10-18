#!/usr/bin/env python3
"""Generate/resume one textured T-pose character without changing the head pipeline.

API contract: https://docs.meshy.ai/en/api/multi-image-to-3d
The caller must authorize credential use and Meshy credits before submitting.
Never prints the key or image data. A task receipt is saved before polling.
"""
import argparse
import base64
import json
import time
from pathlib import Path

import requests

ROOT = Path(__file__).resolve().parents[1]
API = "https://api.meshy.ai/openapi/v1/multi-image-to-3d"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", nargs="+", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--task", help="Resume an existing task; never resubmits")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    payload = {
        "ai_model": "meshy-7.1",
        "geometry_resolution": "standard",
        "should_texture": True,
        "enable_pbr": True,
        "texture_resolution": "2k",
        "should_remesh": True,
        "topology": "triangle",
        "target_polycount": 30000,
        "pose_mode": "t-pose",
        "image_enhancement": False,
        "remove_lighting": True,
        "moderation": True,
        "target_formats": ["glb", "fbx", "obj"],
        "multi_view_thumbnails": True,
    }
    if not args.task:
        if not args.images or not 1 <= len(args.images) <= 4:
            parser.error("Provide 1–4 images, front view first")
        for path in args.images:
            if not path.is_file() or path.stat().st_size > 20 * 1024 * 1024:
                parser.error(f"Missing or oversized image: {path}")
    if args.dry_run:
        print(json.dumps(dict(payload, images=[str(p) for p in args.images or []]), indent=2))
        return
    if not args.task and (args.out / "task.json").exists():
        parser.error("Task receipt exists; resume it with --task instead")
    key = None
    for line in (ROOT / ".env").read_text().splitlines():
        if line.startswith("MESHY_API_KEY="):
            key = line.split("=", 1)[1].strip().strip("\"'")
    if not key:
        parser.error("MESHY_API_KEY is not configured; no task submitted")
    session = requests.Session()
    session.headers["Authorization"] = "Bearer " + key
    args.out.mkdir(parents=True, exist_ok=True)
    if args.task:
        task_id = args.task
    else:
        payload["image_urls"] = [
            "data:image/png;base64," + base64.b64encode(p.read_bytes()).decode()
            for p in args.images
        ]
        (args.out / "request.json").write_text(json.dumps({
            **{k: v for k, v in payload.items() if k != "image_urls"},
            "image_files": [str(p.resolve()) for p in args.images],
        }, indent=2))
        response = session.post(API, json=payload, timeout=120)
        if not response.ok:
            raise SystemExit(f"Meshy submission HTTP {response.status_code}: {response.text[:400]}")
        task_id = response.json()["result"]
        (args.out / "task.json").write_text(json.dumps({"id": task_id, "status": "SUBMITTED"}, indent=2))
        print("Submitted Meshy task", task_id, flush=True)
    deadline = time.monotonic() + 1800
    last = None
    while True:
        response = session.get(API + "/" + task_id, timeout=60)
        if not response.ok:
            raise SystemExit(f"Poll HTTP {response.status_code}; resume with --task {task_id}")
        task = response.json()
        (args.out / "task.json").write_text(json.dumps(task, indent=2))
        state = task.get("status"), task.get("progress")
        if state != last:
            print("Meshy:", *state, flush=True)
            last = state
        if state[0] in {"SUCCEEDED", "FAILED", "CANCELED", "EXPIRED"}:
            break
        if time.monotonic() > deadline:
            raise SystemExit(f"Polling deadline; resume with --task {task_id}")
        time.sleep(15)
    if task["status"] != "SUCCEEDED":
        raise SystemExit(f"Meshy {task['status']}: {task.get('task_error')}")
    # Do not send the API Authorization header to asset download hosts.
    urls = {"model." + ext: url for ext, url in task.get("model_urls", {}).items()
            if ext in {"glb", "fbx", "obj"} and url}
    if task.get("thumbnail_url"):
        urls["thumbnail.png"] = task["thumbnail_url"]
    for view, url in (task.get("thumbnail_urls") or {}).items():
        if url:
            urls[f"preview_{view}.png"] = url
    for index, textures in enumerate(task.get("texture_urls") or []):
        for kind, url in textures.items():
            if url:
                urls[f"texture_{index}_{kind}.png"] = url
    for filename, url in urls.items():
        dest = args.out / filename
        if dest.exists() and dest.stat().st_size:
            continue
        with requests.get(url, timeout=300, stream=True) as download:
            download.raise_for_status()
            partial = dest.with_suffix(dest.suffix + ".partial")
            with partial.open("wb") as output:
                for block in download.iter_content(1024 * 1024):
                    output.write(block)
            partial.replace(dest)
        print("Downloaded", filename, dest.stat().st_size, "bytes", flush=True)
    print("Consumed credits:", task.get("consumed_credits"), flush=True)


if __name__ == "__main__":
    main()
