/* steamworksWebBrowserConfig.js — Steamworks Web Browser Config menu */

function initSteamworksWebBrowserConfig() {
    function onGeneralTab(content) {
        content.innerHTML = '<p style="color:#888; padding:16px;">General settings placeholder.</p>';
    }

    function onAboutTab(content) {
        content.innerHTML = '<p style="color:#888; padding:16px;">About placeholder.</p>';
    }

    arcadeHud.ui.createWindow({
        title: 'Steamworks Web Browser Config',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabs: [
            { label: 'General', onActivate: onGeneralTab },
            { label: 'About',   onActivate: onAboutTab }
        ]
    });
}
