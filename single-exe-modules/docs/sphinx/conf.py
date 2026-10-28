# Configuration file for the Sphinx documentation builder (Breathe + Doxygen XML).
import os
import sys

project = "sc-cmake Single_Exe"
extensions = ["breathe"]
templates_path = ["_templates"]
exclude_patterns = []
html_theme = "sphinx_rtd_theme"

_doxy_xml = os.environ.get(
    "SC_DOXY_XML",
    os.path.abspath(os.path.join(os.path.dirname(__file__), "../../build/docs/doxygen/xml")),
)
breathe_projects = {"sc_cmake": _doxy_xml}
breathe_default_project = "sc_cmake"
