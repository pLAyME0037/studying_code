window.sidebarController = {
    get() {
        return localStorage.getItem('sidebarCollapsed') === 'true';
    },
    apply(collapsed) {
        document.body.classList.toggle('sidebar-collapsed', collapsed);
    },
    toggle() {
        const collapsed = !this.get();
        localStorage.setItem('sidebarCollapsed', collapsed ? 'true' : 'false');
        this.apply(collapsed);
    }
};

(() => {
    window.sidebarController.apply(window.sidebarController.get());
})();

