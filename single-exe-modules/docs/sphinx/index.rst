# Sphinx + Breathe (optional)

```bash
pip install -r docs/requirements-docs.txt
cmake .. -DCXX_ENABLE_DOCS=ON
cmake --build . --target docs          # Doxygen XML/HTML
cmake --build . --target docs-sphinx   # if sphinx-build found
```

See also MkDocs: `cmake --build . --target docs-mkdocs`.
