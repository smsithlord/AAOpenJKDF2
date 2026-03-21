# AArcade UI Framework

The UI framework provides a consistent windowed interface for all AArcade overlay menus. It lives in `src/aarcadecore/ui/` and is built on two files:

- **`arcadeHud.css`** — Shared styles for windows, buttons, and components
- **`arcadeHud.js`** — `arcadeHud.ui.*` API for creating windows programmatically

## Quick Start

Every menu page follows this pattern:

```html
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <link rel="stylesheet" href="arcadeHud.css">
    <script src="arcadeHud.js"></script>
    <script src="myPage.js"></script>
</head>
<body>
    <script>
        if (window.arcadeHud) initMyPage();
        else window.addEventListener('load', function() { setTimeout(initMyPage, 50); });
    </script>
</body>
</html>
```

```javascript
// myPage.js
function initMyPage() {
    var content = arcadeHud.ui.createWindow({
        title: 'My Page',
        showBack: true,
        showClose: true,
        onBack: function() { aapi.manager.openMainMenu(); },
        onClose: function() { aapi.manager.closeMenu(); }
    });

    content.innerHTML = '<button class="aa-btn">Do Something</button>';
}
```

## `arcadeHud.ui.createWindow(options)`

Creates a draggable window with title bar, content area, optional footer, and help text flyout. Returns the `.aa-content` HTMLElement for the page to populate.

### Options

| Option | Type | Description |
|--------|------|-------------|
| `title` | string | Window title (displayed in green on the title bar) |
| `showBack` | boolean | Show a "Back" button in the title bar |
| `showClose` | boolean | Show a close (X) button in the title bar |
| `onBack` | function | Callback when Back is clicked |
| `onClose` | function | Callback when X is clicked |
| `footerButtons` | array | Optional footer buttons: `[{ label, className, onClick }]` |
| `tabs` | array | Tab definitions: `[{ label, onActivate(contentEl) }]` |
| `tabPosition` | string | `'bottom'` to place tabs below content (default: top) |
| `onReady` | function | Called with content element after window is built (non-tabbed pages) |
| `windowClass` | string | Additional CSS class(es) added to `.aa-window` |

### Window Structure

```
.aa-window-wrapper
  .aa-window
    .aa-titlebar.aa-draggable
      .aa-title
      .aa-titlebar-buttons
        button.aa-titlebar-btn        (Back)
        button.aa-titlebar-btn.aa-btn-close  (X)
    .aa-tabs                           (if tabs provided, top position)
      button.aa-tab.aa-tab-active
      button.aa-tab
    .aa-content                        ← returned element
    .aa-tabs.aa-tabs-bottom            (if tabs provided, bottom position)
    .aa-footer                         (if footerButtons provided)
  .aa-helptext                         (flyout below window, absolute positioned)
```

## Tabs

When `tabs` is provided, a row of tab buttons appears (top or bottom). Clicking a tab clears the content area and calls that tab's `onActivate(contentEl)` callback. The first tab is activated on creation.

```javascript
arcadeHud.ui.createWindow({
    title: 'My Tabs',
    tabPosition: 'bottom',
    tabs: [
        { label: 'Tab A', onActivate: function(el) { el.innerHTML = 'Content A'; } },
        { label: 'Tab B', onActivate: function(el) { el.innerHTML = 'Content B'; } }
    ]
});
```

## Dragging

The title bar is draggable. On first drag, the window switches from flex-centered to absolute positioning and follows the cursor. Click-and-drag the title bar to reposition the window.

## Help Text System

Add a `helpText` attribute to any HTML element inside the window:

```html
<button class="aa-btn" helpText="Opens the library browser.">Library</button>
```

When the user hovers over an element with `helpText` (or any ancestor with it), a flyout panel appears below the window showing the help text. It disappears when the mouse moves away.

## CSS Classes

### Buttons

| Class | Usage |
|-------|-------|
| `.aa-btn` | Standard full-width button (green hover) |
| `.aa-btn.aa-btn-danger` | Destructive action (red hover) |
| `.aa-btn.aa-btn-muted` | Subtle/cancel action (gray) |

### Content Components

| Class | Usage |
|-------|-------|
| `.aa-subtitle` | Gray subtitle text |
| `.aa-object-title` | Large white object name |
| `.aa-object-info` | Small gray detail text (URLs, IDs) |
| `.aa-empty-message` | Centered "no items" message |

### Task List

| Class | Usage |
|-------|-------|
| `.aa-task-item` | Flex row for a task entry |
| `.aa-task-info` | Info column (title + URL) |
| `.aa-task-title` | Task title text |
| `.aa-task-url` | Task URL (small, gray) |
| `.aa-task-close` | Close button for a task item |

### Footer (future use)

The `footerButtons` option creates a `.aa-footer` row at the bottom with right-aligned inline buttons (e.g. Cancel/OK). Footer buttons use `.aa-btn` but are auto-styled as inline with smaller padding.

```javascript
arcadeHud.ui.createWindow({
    title: 'Confirm',
    showClose: true,
    onClose: onCancel,
    footerButtons: [
        { label: 'Cancel', className: 'aa-btn-muted', onClick: onCancel },
        { label: 'OK', onClick: onConfirm }
    ]
});
```

## Reusable Components

| Function | Description |
|----------|-------------|
| `arcadeHud.ui.escapeHtml(str)` | Escape HTML special characters for safe insertion |
| `arcadeHud.ui.renderTaskList(containerEl)` | Render active task list with close buttons into any container |
| `arcadeHud.ui.renderLibrary(containerEl)` | Render library browser with grid, search, display modes, and type filters |

### Library Component

`renderLibrary` creates a self-contained library browser with:
- **Display mode slider** (left): List → Small Squares → Small Landscape → Medium Landscape → Dynamic
- **Search box** (center, flex-grows to fill space): 300ms debounce
- **Type toggle buttons** (right): Items, Apps, Maps, Models, Instances (with icons + helpText)
- **Scrollable grid** with "Load More" pagination
- **Initial state**: shows "Type into the SEARCH box or select a favorites list."
- Click an item card to spawn it

The library wrapper sets `min-width: 30vw; min-height: 30vh; max-width: 80vw; height: 60vh` to control its size within the tab content area.

## Existing Pages Using the Framework

| Page | Title | Features |
|------|-------|----------|
| `mainMenu.html/js` | AArcade | Close button, navigation buttons with helpText |
| `taskMenu.html/js` | Active Tasks | Back + Close, uses `renderTaskList` component |
| `buildContextMenu.html/js` | Build | Close, aimed object info, Move/Destroy buttons |
| `tabMenu.html/js` | AArcade (tabs) | Back + Close, bottom tabs: Tasks + Library |

## JS Bridge (`aapi.manager.*`)

All pages communicate with the C++ backend through the `aapi.manager` namespace. Common methods:

| Method | Description |
|--------|-------------|
| `closeMenu()` | Close the overlay menu |
| `openMainMenu()` | Navigate to the main menu page |
| `openLibraryBrowser()` | Open the library browser |
| `openTaskMenu()` | Open the task menu |
| `openBuildContextMenu()` | Open the build context menu |
| `getAimedObjectInfo()` | Get data about the object under the player's crosshair |
| `destroyAimedObject()` | Destroy the aimed-at object |
| `getActiveInstances()` | Get list of active embedded instances |
| `deactivateInstance(itemId)` | Deactivate a specific instance |
| `openTabMenu()` | Open the tabbed control hub |
