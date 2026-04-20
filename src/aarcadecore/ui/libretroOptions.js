/* Per-core / per-game Libretro options menu (Phase 7).
 *
 * Uses three JS bridges from the aarcadecore DLL:
 *   aapi.manager.getLibretroActiveInfo()                  → { corePath, coreName, coreVersion } | null
 *   aapi.manager.getLibretroCoreOptions()                 → [{ key, display, default, core_value, game_value, current, values:[] }, ...] | null
 *   aapi.manager.setLibretroCoreOption(key, value, tier)  → bool   (tier "core" | "game"; tier="game" + value="" clears the override)
 *
 * core_value: empty == use declared default.
 * game_value: empty == inherit from core tier.
 */
function initLibretroOptions() {
    var optionsList = [];
    var activeInfo  = null;

    function reloadFromBridge() {
        try {
            activeInfo = (window.aapi && aapi.manager && aapi.manager.getLibretroActiveInfo)
                ? aapi.manager.getLibretroActiveInfo()
                : null;
        } catch (e) { activeInfo = null; }
        try {
            optionsList = (window.aapi && aapi.manager && aapi.manager.getLibretroCoreOptions)
                ? (aapi.manager.getLibretroCoreOptions() || [])
                : [];
        } catch (e) { optionsList = []; }
    }

    /* What the core would see for `opt` if the game override were cleared. */
    function inheritedValueFor(opt) {
        if (opt.core_value) return opt.core_value;
        if (opt.default)    return opt.default;
        return (opt.values && opt.values.length) ? opt.values[0] : '';
    }

    function setOption(key, value, tier) {
        try {
            if (window.aapi && aapi.manager && aapi.manager.setLibretroCoreOption)
                aapi.manager.setLibretroCoreOption(key, value, tier);
        } catch (e) { console.log('[libretroOptions] setLibretroCoreOption error: ' + e); }
        /* Refresh cached state so meta lines + Inherit labels stay accurate. */
        reloadFromBridge();
    }

    /* One row per option: icon | "Display Name (key)" | <select>. Mirrors
     * the createRow shape used in editItem.js but without per-row action
     * buttons (the select itself is the action). */
    function createOptRow(opt, tier) {
        var row = document.createElement('tr');
        row.className = 'aa-edit-row';

        var iconTd = document.createElement('td');
        iconTd.className = 'aa-edit-cell-icon';
        iconTd.innerHTML = '<img class="aa-edit-row-icon" src="icons/optionsicon.png" alt="" onerror="this.style.visibility=\'hidden\'">';
        row.appendChild(iconTd);

        var labelTd = document.createElement('td');
        labelTd.className = 'aa-edit-cell-label';
        var lbl = document.createElement('div');
        lbl.textContent = (opt.display || opt.key) + ':';
        labelTd.appendChild(lbl);
        var subKey = document.createElement('div');
        subKey.style.color = '#888';
        subKey.style.fontSize = '11px';
        subKey.style.fontFamily = 'Consolas, monospace';
        subKey.style.marginTop = '2px';
        subKey.textContent = opt.key;
        labelTd.appendChild(subKey);
        var meta = document.createElement('div');
        meta.style.color = '#999';
        meta.style.fontSize = '12px';
        meta.style.marginTop = '2px';
        if (tier === 'core') {
            meta.textContent = 'Default: ' + (opt.default || '(first value)') + '   ·   Effective: ' + opt.current;
        } else {
            meta.textContent = 'Inherits: ' + inheritedValueFor(opt) + '   ·   Effective: ' + opt.current;
        }
        labelTd.appendChild(meta);
        row.appendChild(labelTd);

        var inputTd = document.createElement('td');
        inputTd.className = 'aa-edit-cell-input';
        var sel = document.createElement('select');
        sel.className = 'aa-edit-row-input aa-edit-row-select';

        if (tier === 'game') {
            var inheritOpt = document.createElement('option');
            inheritOpt.value = '';
            inheritOpt.textContent = 'Inherit (' + inheritedValueFor(opt) + ')';
            sel.appendChild(inheritOpt);
        }

        var coreSelected = opt.core_value || opt.default || ((opt.values || [])[0] || '');
        var gameSelected = opt.game_value;
        for (var i = 0; i < (opt.values || []).length; i++) {
            var val = opt.values[i];
            var o = document.createElement('option');
            o.value = val;
            o.textContent = val;
            sel.appendChild(o);
            if (tier === 'core' ? val === coreSelected : val === gameSelected) {
                sel.value = val;
            }
        }
        if (tier === 'game' && !gameSelected) sel.value = '';

        sel.addEventListener('change', function() {
            setOption(opt.key, sel.value, tier);
            /* Re-render the active tab so meta lines refresh. */
            if (tier === 'core') onCoreTab(currentContent);
            else                 onGameTab(currentContent);
        });
        inputTd.appendChild(sel);
        row.appendChild(inputTd);

        /* Empty actions cell to match other menus' column layout. */
        var actionsTd = document.createElement('td');
        actionsTd.className = 'aa-edit-cell-actions';
        row.appendChild(actionsTd);

        if (typeof arcadeHud !== 'undefined' && arcadeHud.ui && arcadeHud.ui.enhanceSelect) {
            arcadeHud.ui.enhanceSelect(sel);
        }
        return row;
    }

    function buildTab(content, tier) {
        content.innerHTML = '';
        if (!activeInfo) {
            var p = document.createElement('div');
            p.style.padding = '40px 20px';
            p.style.textAlign = 'center';
            p.style.color = '#777';
            p.textContent = 'No active Libretro core. Launch a game first.';
            content.appendChild(p);
            return;
        }
        if (!optionsList.length) {
            var q = document.createElement('div');
            q.style.padding = '40px 20px';
            q.style.textAlign = 'center';
            q.style.color = '#777';
            q.textContent = 'This core declared no options.';
            content.appendChild(q);
            return;
        }
        var form = document.createElement('table');
        form.className = 'aa-edit-form';
        for (var i = 0; i < optionsList.length; i++) {
            form.appendChild(createOptRow(optionsList[i], tier));
        }
        content.appendChild(form);
    }

    var currentContent = null;

    function onCoreTab(content) {
        currentContent = content;
        reloadFromBridge();
        buildTab(content, 'core');
    }

    function onGameTab(content) {
        currentContent = content;
        reloadFromBridge();
        buildTab(content, 'game');
    }

    /* Pre-load so the title can include the core name. */
    reloadFromBridge();
    var title = 'Libretro Options';
    if (activeInfo) {
        var label = activeInfo.coreName || activeInfo.corePath || '';
        if (activeInfo.coreVersion) label += ' v' + activeInfo.coreVersion;
        if (label) title += ': ' + label;
    }

    arcadeHud.ui.createWindow({
        title: title,
        showBack: false,
        showClose: true,
        /* Closing the menu just returns the overlay slot to whatever loaded
         * us (overlay.html). No fullscreen menu to "close" — same effect as
         * back-navigation. */
        onClose: function() { window.history.back(); },
        tabs: [
            { label: 'Core',          onActivate: onCoreTab },
            { label: 'Game Override', onActivate: onGameTab }
        ]
    });
}
