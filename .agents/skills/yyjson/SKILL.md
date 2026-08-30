---
name: yyjson
description: Use yyjson safely in RedXe for parsing, validating, and serializing JSON. Apply when changing Settings.*, reading JSON values, building mutable documents, or reviewing yyjson ownership and string lifetimes.
---

# yyjson

Use yyjson from `vcpkg.json`; do not vendor its source. Apply `wil-raii` to document and write-buffer ownership.

## Lifetime rules

- A `yyjson_doc*` or `yyjson_mut_doc*` owns all values and borrowed strings obtained from it. Never retain a
  `yyjson_val*` or `yyjson_get_str()` result after the document is freed.
- Own documents unconditionally with WIL:

```cpp
using unique_yyjson_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
using unique_yyjson_mut_doc = wil::unique_any<yyjson_mut_doc*, decltype(&yyjson_mut_doc_free), yyjson_mut_doc_free>;
using unique_malloc_string = wil::unique_any<char*, decltype(&free), free>;
```

- Pass a mutable buffer to `yyjson_read_opts()`, capture `yyjson_read_err`, check the root and every value's type, and
  reject invalid ranges before narrowing numeric values.
- Free buffers returned by write APIs with `free()` unless a custom allocator supplied a different free function.

## Mutable strings

- Convenience `yyjson_mut_obj_add_*` functions borrow their keys.
- `*_str` and `*_strn` borrow value bytes; use `*_strcpy` or `*_strncpy` for temporary or converted strings.
- For a dynamic key, allocate both key and value as document-owned `yyjson_mut_val*` values and call
  `yyjson_mut_obj_add()`.
- Check all allocation and add results when complete output is required; treat failures as out of memory.

Run `.\test.ps1` after JSON changes. The smoke path must parse the default settings before initializing Direct3D.
