/* 在标题栏注入「test / release」双版本切换下拉框。 */
(function () {
  function currentVersion() {
    var m = window.location.pathname.match(/\/(test|release)\/html\//);
    return m ? m[1] : null;
  }

  function versionUrl(version) {
    var path = window.location.pathname;
    return path.replace(/\/(test|release)\/html\/.*$/, "/" + version + "/html/index.html");
  }

  document.addEventListener("DOMContentLoaded", function () {
    var current = currentVersion();
    if (!current) {
      return;
    }

    var select = document.createElement("select");
    select.className = "docs-version-select";
    select.setAttribute("aria-label", "文档版本");

    var test = document.createElement("option");
    test.value = versionUrl("test");
    test.textContent = "test（全模块）";

    var release = document.createElement("option");
    release.value = versionUrl("release");
    release.textContent = "release（发布）";

    select.appendChild(test);
    select.appendChild(release);
    select.value = versionUrl(current);
    select.addEventListener("change", function () {
      window.location.href = select.value;
    });

    var wrap = document.createElement("span");
    wrap.className = "docs-version-wrap";
    wrap.appendChild(select);

    var target =
      document.querySelector("#projectrow") ||
      document.querySelector("#projectalign") ||
      document.querySelector("#titlearea");
    if (target) {
      target.appendChild(wrap);
    }
  });
})();
