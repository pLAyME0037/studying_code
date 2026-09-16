window.sidebarController = {
    get() {
        return localStorage.getItem('sidebarCollapsed') === 'true';
    },
    apply(collapsed) {
        document.body.classList.toggle('sidebarCollapsed', collapsed);
    },
    toggle() {
        const collapsed = !this.get();
        localStorage.setItem('sidebarCollapsed', collapsed ? 'true' : 'false');
        this.apply(collapsed);
    },
    scrollToActive() {
        const activeLink = document.querySelector('#sidebar .nav-link.bg-indigo-50');
        if (activeLink && !this.get()) {
            activeLink.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
        }
    }
};

(function init() {
    var collapsed = window.sidebarController.get();
    window.sidebarController.apply(collapsed);
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', function() {
            window.sidebarController.scrollToActive();
        });
    } else {
        window.sidebarController.scrollToActive();
    }
})();

