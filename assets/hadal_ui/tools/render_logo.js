const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ deviceScaleFactor: 2 });
  for (const f of process.argv.slice(2)) {
    const svg = fs.readFileSync(f, 'utf8');
    const m = svg.match(/width="(\d+)" height="(\d+)"/);
    await page.setViewportSize({ width: +m[1], height: +m[2] });
    await page.setContent(`<html><body style="margin:0;background:transparent">${svg}</body></html>`);
    // at twice the size, for sharp pixels on high-density screens; clear where nothing is drawn
    await page.screenshot({ path: f.replace('.svg', '.png'), omitBackground: true });
  }
  await browser.close();
})();
