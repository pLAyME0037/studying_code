window.themeController = {
    get() {
        let t = localStorage.getItem('theme');
        if (!t) t = matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
        return t;
    },
    apply(t) {
        if (t === 'device') t = matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
        document.documentElement.classList.toggle('dark', t === 'dark');
    },
    setTheme(t) {
        localStorage.setItem('theme', t);
        this.apply(t);
    }
};

(() => {
    window.themeController.apply(window.themeController.get());
})();

