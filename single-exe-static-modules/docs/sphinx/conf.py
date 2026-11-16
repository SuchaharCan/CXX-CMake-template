import os

project = "Omni-Stacks"
copyright = "2026, Omni-Stacks"
author = "Omni-Stacks"

extensions = ["breathe", "exhale"]

# Doxygen XML 输出目录：Doxyfile 里 OUTPUT_DIRECTORY=build/docs/doxygen、XML_OUTPUT=xml
_xml_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "build", "docs", "doxygen", "test", "xml")
)
breathe_projects = {"OmniStacks": _xml_dir}
breathe_default_project = "OmniStacks"

exhale_args = {
    "containmentFolder": "./api",
    "rootFileName": "library_root.rst",
    "rootFileTitle": "C++ API",
    "doxygenStripFromPath": "..",
    "createTreeView": True,
}

html_theme = "alabaster"
