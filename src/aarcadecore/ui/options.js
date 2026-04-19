/* options.js — AArcade Options Menu */

function initOptions() {
    function onGeneralTab(content) {
        content.innerHTML = '<p style="color:#888; padding:16px;">General settings placeholder.</p>';
    }

    function onImportTab(content) {
        content.innerHTML = '<p style="color:#888; padding:16px;">Import actions placeholder.</p>';

        /* Hidden re-enable-later controls. Kept attached so the click handlers and
         * status targets survive; all marked display:none so they don't alter the
         * tab's layout height versus the General tab. */
        var status = document.createElement('p');
        status.style.cssText = 'display:none; color:#aaa; padding:8px 0; margin:0;';
        var status2 = document.createElement('p');
        status2.style.cssText = 'display:none; color:#aaa; padding:8px 0; margin:0;';

        var btn = document.createElement('button');
        btn.className = 'aa-btn';
        btn.style.display = 'none';
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
        content.appendChild(status);

        var btn2 = document.createElement('button');
        btn2.className = 'aa-btn';
        btn2.style.cssText = 'display:none; margin-top:12px;';
        btn2.textContent = 'Import Adopted Templates';
        btn2.addEventListener('click', function() {
            var result = null;
            try {
                if (window.aapi && aapi.manager && aapi.manager.importAdoptedTemplates) {
                    result = aapi.manager.importAdoptedTemplates();
                }
            } catch (e) {
                console.log('[options] importAdoptedTemplates error: ' + e);
            }

            if (result) {
                status2.textContent = 'Created ' + result.created + ' of ' + result.total + ' adopted templates.';
            } else {
                status2.textContent = 'Import failed — bridge not available.';
            }
        });
        content.appendChild(btn2);
        content.appendChild(status2);
    }

    function onEmbeddedTab(content) {
        content.innerHTML = '';
        var btn = document.createElement('button');
        btn.className = 'aa-btn';
        btn.textContent = 'Libretro Config';
        btn.addEventListener('click', function() {
            window.location.href = 'file:///ui/libretroConfig.html';
        });
        content.appendChild(btn);

        var btn2 = document.createElement('button');
        btn2.className = 'aa-btn';
        btn2.style.marginTop = '8px';
        btn2.textContent = 'MPV Media Player Config';
        btn2.addEventListener('click', function() {
            window.location.href = 'file:///ui/mpvConfig.html';
        });
        content.appendChild(btn2);

        var btn3 = document.createElement('button');
        btn3.className = 'aa-btn';
        btn3.style.marginTop = '8px';
        btn3.textContent = 'Steamworks Web Browser Config';
        btn3.addEventListener('click', function() {
            window.location.href = 'file:///ui/steamworksWebBrowserConfig.html';
        });
        content.appendChild(btn3);
    }

    arcadeHud.ui.createWindow({
        title: 'Options',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabs: [
            { label: 'General', onActivate: onGeneralTab },
            { label: 'Import', onActivate: onImportTab },
            { label: 'Embedded', onActivate: onEmbeddedTab }
        ]
    });
}
