# Coding Rules

- Use C++20.
- Keep `main.cpp` as the app bootstrap only.
- Prefer explicit data arguments over hidden global state.
- Add new systems only when the vertical slice needs them.
- Keep module ownership clear:
  - app: lifecycle and events.
  - renderer: GPU resources and passes.
  - gameplay: game state.
  - world: map and level data.
  - physics: movement and collision.
  - debug: ImGui panels.
  - assets: loading and caching.
- Keep comments short and useful.
- Avoid broad refactors while the prototype is still proving the core loop.
