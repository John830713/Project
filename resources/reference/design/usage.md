# Usage

## Add infinite scroll from nhentai

1. Copy `addInfiniteStatus()`
2. Copy `fetchNextPage()`, change API URL and render
3. Copy scroll listener (with debounce)
4. Copy `nhRequest()` rate limiter

## Add bidirectional scroll from rule34

1. Copy `loadPrevPage()` and `loadNextPage()`
2. Copy `syncPageIndicator()` sync
3. Add `loadedPids` Set and `consecutiveFailures` counter

## Add page jumper from nhentai

1. Copy `createPageIndicator()` DOM
2. Copy `updatePageIndicator()` state
3. Copy `goToPage()` navigation

## Notes

- All GM_xmlhttpRequest must go through rate-limiting queue
- Scroll trigger uses getBoundingClientRect().bottom
- border-radius: 4px, font-size: 12px, buttons: #ff9800
