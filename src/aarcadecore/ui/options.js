/* options.js — AArcade Options Menu */

function initOptions() {
    function onGeneralTab(content) {
        content.innerHTML = '<p style="color:#888; padding:16px;">No options yet.</p>';
    }

    function onImportTab(content) {
        content.innerHTML = '';

        var btn = document.createElement('button');
        btn.className = 'aa-btn';
        btn.textContent = 'Import Default Library';
        btn.addEventListener('click', function() {
            var result = null;
            try {
                if (window.aapi && aapi.manager && aapi.manager.importDefaultLibrary) {
                    result = aapi.manager.importDefaultLibrary();
                }
            } catch (e) {
                console.log('[options] importDefaultLibrary error: ' + e);
            }

            if (result) {
                status.textContent = 'Created ' + result.created + ' of ' + result.total + ' default models.';
            } else {
                status.textContent = 'Import failed — bridge not available.';
            }
        });
        content.appendChild(btn);

        var status = document.createElement('p');
        status.style.cssText = 'color:#aaa; padding:8px 0; margin:0;';
        content.appendChild(status);
    }

    arcadeHud.ui.createWindow({
        title: 'Options',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabs: [
            { label: 'General', onActivate: onGeneralTab },
            { label: 'Import', onActivate: onImportTab }
        ]
    });
}
