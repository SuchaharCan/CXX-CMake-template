/*
 * 首次访问默认深色；之后完全交给 Doxygen 的 darkmode_toggle.js 管理。
 *
 * 不要在这里直接切换 html.dark-mode：那样会让页面 class 与 DarkModeToggle
 * 内部状态不同步，造成点击图标第一次无效。
 */
(function () {
  "use strict";

  const initializedCookie = "docs-theme-initialized";

  function hasCookie(name) {
    return document.cookie.split("; ").some(function (item) {
      return item.indexOf(name + "=") === 0;
    });
  }

  function rememberInitialization() {
    document.cookie =
      initializedCookie +
      "=1; path=/; max-age=31536000; SameSite=Lax";
  }

  function initializeDefaultTheme() {
    if (typeof DarkModeToggle === "undefined") {
      return;
    }

    // 已有偏好或此前已经初始化过，则尊重用户和系统设置。
    if (
      hasCookie(initializedCookie) ||
      hasCookie("prefers-dark") ||
      hasCookie("prefers-light")
    ) {
      rememberInitialization();
      return;
    }

    // 通过 Doxygen 原生 setter 同步 cookie、内部状态和页面 class。
    DarkModeToggle.userPreference = true;
    rememberInitialization();
  }

  // darkmode_toggle.js 在上一段同步脚本中已定义 DarkModeToggle。
  initializeDefaultTheme();
})();
