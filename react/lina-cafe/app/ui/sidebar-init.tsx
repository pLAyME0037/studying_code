"use client";

import { useEffect } from "react";

// Re-runs SidebarManager.init() after every admin page mount: client
// navigations recreate the sidebar DOM (active link is server-computed,
// but tooltip listeners and state sync need a refresh). The manager
// itself loads from /js/sidebarSwitcher.js.
export function SidebarInit() {
  useEffect(() => {
    const w = window as unknown as {
      SidebarManager?: { init: () => void };
    };
    w.SidebarManager?.init();
  });
  return null;
}
