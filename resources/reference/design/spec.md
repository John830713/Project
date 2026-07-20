# Design Preferences Specification

## Infinite scroll

### Trigger pattern (direct copy)

```javascript
window.addEventListener('scroll', function() {
    if (isLoading || !hasMore) return;
    var grid = document.querySelector('.gallery-grid');
    if (!grid) return;
    var rect = grid.getBoundingClientRect();
    if (rect.bottom <= window.innerHeight + 400) {
        fetchNextPage();
    }
});
```

- Use `getBoundingClientRect().bottom` instead of `scrollHeight`
- Pre-trigger distance `400px`
- Debounce scroll events 200ms

### Bidirectional infinite scroll (Rule34 pattern)

- Above: `y <= 200` triggers previous page
- Below: `y >= dh - vh - 400` triggers next page
- After load to top, keep reading position with `window.scrollTo(0, oldScrollY + addedH)`
- Use `loadedPids` Set to prevent duplicate loads

### Rate Limiting (mandatory for all API requests)

```javascript
var reqQueue = [];
var reqBusy = false;

function nhRequest(opts) {
    reqQueue.push(opts);
    if (!reqBusy) processNext();
}
function processNext() {
    if (reqQueue.length === 0) { reqBusy = false; return; }
    reqBusy = true;
    var opts = reqQueue.shift();
    setTimeout(function() {
        GM_xmlhttpRequest({
            method: opts.method || 'GET',
            url: opts.url,
            onload: function(r) { opts.onload(r); reqBusy = false; processNext(); },
            onerror: function(e) { if (opts.onerror) opts.onerror(e); reqBusy = false; processNext(); },
            ontimeout: function() { if (opts.ontimeout) opts.ontimeout(); reqBusy = false; processNext(); }
        });
    }, 800);
}
```

## Page jumper (nhentai pattern)

### Component structure: `⇤ ‹ [X / Y] › ⇥ [input] [jump]`

### Core functions: createPageIndicator, updatePageIndicator, goToPage, syncPageIndicator

## Overlay button style

```css
.dl-overlay-btn {
    width: 100%; padding: 5px 0;
    background: rgba(252, 148, 0, 0.9);
    color: #fff; font-size: 12px; font-weight: bold;
    border: none; border-radius: 4px 4px 0 0; cursor: pointer;
}
```

## Progress bar

```css
.dl-progress-outer { width: 100%; height: 8px; background: #e0e0e0; border-radius: 0 0 4px 4px; overflow: hidden; }
.dl-progress-inner { width: 0%; height: 100%; background: #4caf50; transition: width 0.1s linear; }
```

## General CSS norms

| Property | Value | Usage |
|----------|-------|-------|
| border-radius | 4px | all corners |
| font-size | 12px | standard UI |
| primary btn | #ff9800 | orange |
| progress | #4caf50 | green |
| error/close | #c0392b | red |
