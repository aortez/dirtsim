import path from "node:path";
import fs from "node:fs/promises";
import { PNG } from "pngjs";

const rootDir = process.cwd();
const realDir = path.resolve(
  rootDir,
  process.env.DOCS_REAL_DIR ?? "screenshots/real"
);
const markdownDir = path.resolve(
  rootDir,
  process.env.DOCS_MARKDOWN_DIR ?? "screenshots/markdown"
);
const outputDir = path.resolve(
  rootDir,
  process.env.DOCS_COMPARE_DIR ?? "screenshots/compare"
);

const only = process.env.DOCS_SCREENSHOT_ONLY
  ? process.env.DOCS_SCREENSHOT_ONLY.split(",").map((item) => item.trim())
  : null;

const labelHeight = 8;
const dividerHeight = 10;
const realLabelColor = { r: 192, g: 57, b: 43, a: 255 };
const markdownLabelColor = { r: 41, g: 128, b: 185, a: 255 };
const dividerColor = { r: 230, g: 230, b: 230, a: 255 };
const backgroundColor = { r: 255, g: 255, b: 255, a: 255 };

const readPng = async (filePath) =>
  PNG.sync.read(await fs.readFile(filePath));

const fillRect = (png, x, y, width, height, color) => {
  const startX = Math.max(0, x);
  const startY = Math.max(0, y);
  const endX = Math.min(png.width, x + width);
  const endY = Math.min(png.height, y + height);
  for (let row = startY; row < endY; row += 1) {
    for (let col = startX; col < endX; col += 1) {
      const index = (png.width * row + col) * 4;
      png.data[index] = color.r;
      png.data[index + 1] = color.g;
      png.data[index + 2] = color.b;
      png.data[index + 3] = color.a;
    }
  }
};

const fillBackground = (png, color) => {
  for (let i = 0; i < png.data.length; i += 4) {
    png.data[i] = color.r;
    png.data[i + 1] = color.g;
    png.data[i + 2] = color.b;
    png.data[i + 3] = color.a;
  }
};

const listPngBases = async (dir) => {
  try {
    const entries = await fs.readdir(dir, { withFileTypes: true });
    return entries
      .filter((entry) => entry.isFile() && entry.name.endsWith(".png"))
      .map((entry) => path.basename(entry.name, ".png"));
  } catch {
    return [];
  }
};

const listScreenIds = async () => {
  try {
    const screensDir = path.join(rootDir, "src", "screens");
    const entries = await fs.readdir(screensDir, { withFileTypes: true });
    return entries
      .filter((entry) => entry.isFile() && entry.name.endsWith(".md"))
      .map((entry) => path.basename(entry.name, ".md"));
  } catch {
    return [];
  }
};

const run = async () => {
  await fs.mkdir(outputDir, { recursive: true });

  const [realBases, markdownBases, screenIds] = await Promise.all([
    listPngBases(realDir),
    listPngBases(markdownDir),
    listScreenIds()
  ]);

  const realSet = new Set(realBases);
  const markdownSet = new Set(markdownBases);

  const expectedScreens = screenIds.length > 0 ? screenIds : realBases;
  const missingMarkdown = expectedScreens.filter((name) => !markdownSet.has(name));
  const missingReal = expectedScreens.filter((name) => !realSet.has(name));

  if (missingMarkdown.length > 0) {
    console.warn(
      `Missing markdown screenshots: ${missingMarkdown.join(", ")}`
    );
  }
  if (missingReal.length > 0) {
    console.warn(`Missing real screenshots: ${missingReal.join(", ")}`);
  }

  let names = expectedScreens.filter(
    (name) => realSet.has(name) && markdownSet.has(name)
  );
  if (only) {
    names = names.filter((name) => only.includes(name));
  }

  if (names.length === 0) {
    console.warn("No matching screenshots to stack.");
    return;
  }

  for (const name of names) {
    const realPath = path.join(realDir, `${name}.png`);
    const markdownPath = path.join(markdownDir, `${name}.png`);
    const [realPng, markdownPng] = await Promise.all([
      readPng(realPath),
      readPng(markdownPath)
    ]);

    const width = Math.max(realPng.width, markdownPng.width);
    const height =
      labelHeight +
      realPng.height +
      dividerHeight +
      labelHeight +
      markdownPng.height;

    const output = new PNG({ width, height });
    fillBackground(output, backgroundColor);

    fillRect(output, 0, 0, width, labelHeight, realLabelColor);
    const realOffsetX = Math.floor((width - realPng.width) / 2);
    PNG.bitblt(
      realPng,
      output,
      0,
      0,
      realPng.width,
      realPng.height,
      realOffsetX,
      labelHeight
    );

    const dividerY = labelHeight + realPng.height;
    fillRect(output, 0, dividerY, width, dividerHeight, dividerColor);

    const markdownLabelY = dividerY + dividerHeight;
    fillRect(
      output,
      0,
      markdownLabelY,
      width,
      labelHeight,
      markdownLabelColor
    );
    const markdownOffsetX = Math.floor((width - markdownPng.width) / 2);
    PNG.bitblt(
      markdownPng,
      output,
      0,
      0,
      markdownPng.width,
      markdownPng.height,
      markdownOffsetX,
      markdownLabelY + labelHeight
    );

    const outputPath = path.join(outputDir, `${name}.png`);
    await fs.writeFile(outputPath, PNG.sync.write(output));
    console.log(`Stacked ${name} -> ${path.relative(rootDir, outputPath)}`);
  }
};

run().catch((error) => {
  console.error(error);
  process.exit(1);
});
