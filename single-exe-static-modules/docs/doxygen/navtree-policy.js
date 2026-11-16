/*
 * Omni-Stacks Doxygen 侧栏策略：
 *   1. 移除项目名根节点，将页面/API 分类提升为一级节点；
 *   2. 默认展开到第 3 层，第 4 层及更深节点保持折叠；
 *   3. 每个节点只应用一次策略，之后允许用户手动展开或折叠。
 */
(function () {
  "use strict";

  const maxExpandedDepth = 3;
  const deadline = Date.now() + 30000;
  const processedNodes = new WeakSet();
  let projectRootRemoved = false;

  function directChildren(element, tagName) {
    return Array.from(element.children).filter(function (child) {
      return child.tagName === tagName;
    });
  }

  function directList(item) {
    return directChildren(item, "UL")[0] || null;
  }

  function directToggle(item) {
    const itemRow = directChildren(item, "DIV")[0] || item;
    return directChildren(itemRow, "A").find(function (link) {
      return link.querySelector(".arrowhead") !== null;
    }) || null;
  }

  function arrowOf(toggle) {
    return toggle ? toggle.querySelector(".arrowhead") : null;
  }

  function flattenProjectRoot(rootList) {
    if (projectRootRemoved) {
      return true;
    }

    const projectItem = directChildren(rootList, "LI")[0];
    if (!projectItem) {
      return false;
    }

    const projectToggle = directToggle(projectItem);
    const projectArrow = arrowOf(projectToggle);
    if (!projectToggle || !projectArrow) {
      return false;
    }

    if (projectArrow.classList.contains("closed")) {
      projectToggle.click();
    }

    const projectChildren = directList(projectItem);
    if (!projectChildren) {
      return false;
    }

    // 原项目的直接子项成为侧栏一级节点。
    while (projectChildren.firstElementChild) {
      rootList.appendChild(projectChildren.firstElementChild);
    }
    projectItem.remove();
    projectRootRemoved = true;
    return true;
  }

  function applyDepthPolicy(list, depth) {
    directChildren(list, "LI").forEach(function (item) {
      if (!processedNodes.has(item)) {
        const toggle = directToggle(item);
        const arrow = arrowOf(toggle);
        if (toggle && arrow) {
          const shouldExpand = depth < maxExpandedDepth;
          if (shouldExpand && arrow.classList.contains("closed")) {
            toggle.click();
          } else if (!shouldExpand && arrow.classList.contains("opened")) {
            toggle.click();
          }
        }
        processedNodes.add(item);
      }

      const childList = directList(item);
      if (childList) {
        applyDepthPolicy(childList, depth + 1);
      }
    });
  }

  function applyPolicy() {
    const rootList = document.querySelector("#nav-tree-contents > ul");
    if (rootList && flattenProjectRoot(rootList)) {
      applyDepthPolicy(rootList, 1);
    }
  }

  function start() {
    const timer = window.setInterval(function () {
      applyPolicy();
      if (Date.now() >= deadline) {
        window.clearInterval(timer);
      }
    }, 100);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", function () {
      window.setTimeout(start, 0);
    }, { once: true });
  } else {
    window.setTimeout(start, 0);
  }
})();
