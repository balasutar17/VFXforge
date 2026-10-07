# Third-party code

Both libraries are vendored in `third_party/` so the project builds with no
network access. Their licence texts sit beside them.

| Library | Version | Licence | Used by |
| --- | --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | MIT | `core` (hidden behind `core/src/Json.h`) |
| [Catch2](https://github.com/catchorg/Catch2) | 3.5.4 | BSL-1.0 | tests only; not part of any shipped binary |
