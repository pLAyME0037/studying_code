// Port of webc_pos js/sidebarSwitcher.js — identical behavior, one change:
// element-level click bindings became document-level delegation (Next.js
// client navigations reuse/replace sidebar DOM, so per-element listeners
// bound at load would go stale; delegation survives DOM swaps). init()
// stays re-runnable (active-link refresh + tooltip rebind per mount).
window.SidebarManager = {
    storageKey: 'sidebar_collapsed',
    _bound: false,

    // 1. Get current collapsed state
    isCollapsed() {
        return localStorage.getItem(this.storageKey) === 'true';
    },

    // 2. Set collapsed state to HTML & localStorage
    setCollapsed(collapsed) {
        localStorage.setItem(this.storageKey, collapsed ? 'true' : 'false');
        document.documentElement.classList.toggle('sidebar-collapsed', collapsed);
    },

    // 3. Toggle state (mobile: overlay drawer, desktop: collapse rail)
    isMobile() {
        return window.matchMedia('(max-width: 767.98px)').matches;
    },

    isMobileOpen() {
        return document.documentElement.classList.contains('mobile-sidebar-open');
    },

    setMobileOpen(open) {
        document.documentElement.classList.toggle('mobile-sidebar-open', open);
    },

    toggle() {
        if (this.isMobile()) {
            this.setMobileOpen(!this.isMobileOpen());
        } else {
            this.setCollapsed(!this.isCollapsed());
        }
    },

    // 4. Highlight the current active link based on URL
    updateActiveLink() {
        const currentPath = window.location.pathname;
        const links = document.querySelectorAll('#sidebar a[href]');

        let activeLink = null;

        links.forEach((link) => {
            const linkPath = new URL(link.href, window.location.origin).pathname;

            // Match current route (exact or base match depending on your needs)
            const isActive = currentPath === linkPath || (linkPath !== '/' && currentPath.startsWith(linkPath));

            link.classList.toggle('active', isActive);
            if (isActive) {
                link.setAttribute('aria-current', 'page');
                activeLink = link;

                // If the active item is inside a collapsed accordion/dropdown, expand it
                const parentAccordion = link.closest('.sidebar-group');
                if (parentAccordion) {
                    parentAccordion.classList.add('open');
                }
            } else {
                link.removeAttribute('aria-current');
            }
        });

        return activeLink;
    },

    // 5. Scroll active link into view
    scrollToActive(activeElement) {
        const target = activeElement || document.querySelector('#sidebar a.active');

        // Only scroll if sidebar is expanded and active element exists
        if (target && !this.isCollapsed()) {
            target.scrollIntoView({
                behavior: 'smooth',
                block: 'nearest',
                inline: 'nearest'
            });
        }
    },

    // 6. Position tooltips when collapsed (per-trigger guard so repeated
    // init() calls on a reused DOM never stack duplicate listeners)
    positionTooltips() {
        if (!this.isCollapsed()) return;

        const triggers = document.querySelectorAll('#sidebar .group\\\/item, #sidebar .user-card');
        triggers.forEach((trigger) => {
            const tooltip = trigger.querySelector('.sidebar-tooltip');
            if (!tooltip) return;
            if (trigger.dataset.tooltipBound) return;
            trigger.dataset.tooltipBound = '1';

            trigger.addEventListener('mouseenter', () => {
                const rect = trigger.getBoundingClientRect();
                tooltip.style.left = (rect.right + 8) + 'px';
                tooltip.style.top = (rect.top + rect.height / 2) + 'px';
                tooltip.style.transform = 'translateY(-50%)';
            });
        });
    },

    // 7. One-time document-level bindings (delegated: survive DOM swaps)
    bindGlobal() {
        if (this._bound) return;
        this._bound = true;

        // Toggle buttons (any element with data-sidebar-toggle) + mobile
        // close (backdrop / any element with data-sidebar-close)
        document.addEventListener('click', (e) => {
            const t = e.target && e.target.closest && e.target.closest('[data-sidebar-toggle]');
            if (t) { this.toggle(); return; }
            const c = e.target && e.target.closest && e.target.closest('[data-sidebar-close]');
            if (c) this.setMobileOpen(false);
        });

        // Escape closes the mobile drawer
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') this.setMobileOpen(false);
        });

        // Leaving the mobile viewport closes the drawer
        window.addEventListener('resize', () => {
            if (!this.isMobile() && this.isMobileOpen()) this.setMobileOpen(false);
        });
    },

    // 8. Initialization (re-runnable: called on every admin page mount)
    init() {
        // Sync state
        this.setCollapsed(this.isCollapsed());

        // Highlight & scroll to active item
        const active = this.updateActiveLink();
        this.scrollToActive(active);

        // Position tooltips
        this.positionTooltips();

        // Bind delegated document listeners once
        this.bindGlobal();
    }
};

// Auto-run on DOM ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => window.SidebarManager.init());
} else {
    window.SidebarManager.init();
}
