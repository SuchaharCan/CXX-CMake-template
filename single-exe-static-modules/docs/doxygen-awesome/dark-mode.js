/* 默认启用深色模式；用户通过 Doxygen 原生开关切换后记住选择。 */
(function () {
  function setCookie(name, value) {
    document.cookie = name + "=" + value + "; path=/";
  }
  function clearCookie(name) {
    document.cookie = name + "=; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT";
  }
  function applyDark(dark) {
    document.documentElement.classList.toggle("dark-mode", dark);
    document.documentElement.classList.toggle("light-mode", !dark);
    if (dark) {
      setCookie("prefers-dark", "1");
      clearCookie("prefers-light");
    } else {
      setCookie("prefers-light", "1");
      clearCookie("prefers-dark");
    }
    document.querySelectorAll(".dark-mode-visible").forEach(function (el) {
      el.style.display = dark ? "inline-block" : "none";
    });
    document.querySelectorAll(".light-mode-visible").forEach(function (el) {
      el.style.display = dark ? "none" : "inline-block";
    });
  }

  var savedDark = document.cookie.indexOf("prefers-dark") !== -1;
  var savedLight = document.cookie.indexOf("prefers-light") !== -1;
  // 默认深色：只有用户明确选过浅色时才用浅色
  applyDark(savedDark || !savedLight);
})();
