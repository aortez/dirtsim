import { chromium, firefox, webkit } from "playwright";
import { build } from "vite";
import path from "node:path";
import fs from "node:fs/promises";
import fsSync from "node:fs";
import http from "node:http";
import { spawn } from "node:child_process";
import { marked } from "marked";

const rootDir = process.cwd();
const outputDir = path.join(rootDir, "screenshots", "markdown");
const buildDir = path.join(rootDir, "screenshots", ".tmp-build");
const browserTypes = { chromium, firefox, webkit };
const plantumlAssetDir = path.join(buildDir, "plantuml");
const plantumlLanguages = new Set(["plantuml", "puml", "salt"]);
const dockerDir = path.resolve(rootDir, "../../../../docker");
let dockerImage = null;

const contentTypes = {
  ".css": "text/css",
  ".js": "text/javascript",
  ".html": "text/html",
  ".json": "application/json",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".gif": "image/gif",
  ".woff": "font/woff",
  ".woff2": "font/woff2"
};

const buildDocs = async () => {
  await build({
    root: rootDir,
    logLevel: "error",
    base: "./",
    build: {
      outDir: buildDir,
      emptyOutDir: true
    }
  });
};

const normalizePlantUml = (value) =>
  String(value ?? "")
    .replace(/\r\n/g, "\n")
    .replace(/[ \t]+$/gm, "")
    .replace(/\n+$/, "");

const hashPlantUml = (value) => {
  const text = normalizePlantUml(value);
  let hash = 0xcbf29ce484222325n;
  const prime = 0x100000001b3n;
  for (let i = 0; i < text.length; i += 1) {
    hash ^= BigInt(text.charCodeAt(i));
    hash = (hash * prime) & 0xffffffffffffffffn;
  }
  return hash.toString(16).padStart(16, "0");
};

const ensureDockerImage = async () => {
  if (dockerImage) {
    return dockerImage;
  }
  await new Promise((resolve, reject) => {
    const child = spawn("make", ["-C", dockerDir, "build-image"], {
      stdio: "inherit"
    });
    child.on("close", (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`Failed to build docker image (${code}).`));
      }
    });
  });
  dockerImage = await new Promise((resolve, reject) => {
    const child = spawn(
      "make",
      ["-C", dockerDir, "--no-print-directory", "print-image"],
      { stdio: ["ignore", "pipe", "pipe"] }
    );
    let output = "";
    let errorOutput = "";
    child.stdout.on("data", (chunk) => {
      output += chunk.toString();
    });
    child.stderr.on("data", (chunk) => {
      errorOutput += chunk.toString();
    });
    child.on("close", (code) => {
      if (code === 0) {
        const lines = output
          .split(/\r?\n/)
          .map((line) => line.trim())
          .filter(Boolean);
        resolve(lines[lines.length - 1] ?? "");
      } else {
        reject(
          new Error(
            `Failed to resolve docker image (${code}): ${errorOutput || ""}`
          )
        );
      }
    });
  });
  if (!dockerImage) {
    throw new Error("Docker image tag is empty.");
  }
  return dockerImage;
};

const renderPlantUmlSvg = async (code) => {
  const image = await ensureDockerImage();
  return new Promise((resolve, reject) => {
    const child = spawn(
      "docker",
      ["run", "--rm", "-i", image, "plantuml", "-tsvg", "-pipe"],
      { stdio: ["pipe", "pipe", "pipe"] }
    );
    let output = "";
    let errorOutput = "";
    child.stdout.on("data", (chunk) => {
      output += chunk.toString();
    });
    child.stderr.on("data", (chunk) => {
      errorOutput += chunk.toString();
    });
    child.on("close", (codeValue) => {
      if (codeValue === 0) {
        resolve(output);
      } else {
        reject(
          new Error(
            `PlantUML docker render failed (${codeValue}): ${
              errorOutput || "Unknown error."
            }`
          )
        );
      }
    });
    child.stdin.write(code);
    child.stdin.end();
  });
};

const collectPlantUmlBlocks = async () => {
  const screensDir = path.join(rootDir, "src", "screens");
  const entries = await fs.readdir(screensDir, { withFileTypes: true });
  const markdownFiles = entries
    .filter((entry) => entry.isFile() && entry.name.endsWith(".md"))
    .map((entry) => path.join(screensDir, entry.name));
  const blocks = [];
  for (const filePath of markdownFiles) {
    const content = await fs.readFile(filePath, "utf8");
    const tokens = marked.lexer(content);
    for (const token of tokens) {
      if (token.type !== "code") {
        continue;
      }
      const language = (token.lang || "").trim().toLowerCase();
      if (!plantumlLanguages.has(language)) {
        continue;
      }
      blocks.push(normalizePlantUml(token.text));
    }
  }
  return blocks;
};

const renderPlantUmlAssets = async (blocks) => {
  if (blocks.length === 0) {
    return;
  }
  await fs.mkdir(plantumlAssetDir, { recursive: true });
  const unique = new Map();
  for (const block of blocks) {
    const encoded = hashPlantUml(block);
    if (!unique.has(encoded)) {
      unique.set(encoded, block);
    }
  }
  for (const [encoded, block] of unique.entries()) {
    const outputPath = path.join(plantumlAssetDir, `${encoded}.svg`);
    if (fsSync.existsSync(outputPath)) {
      continue;
    }
    const svg = await renderPlantUmlSvg(block);
    await fs.writeFile(outputPath, svg);
  }
};

const waitForMermaid = async (page) => {
  await page.waitForFunction(() => {
    const target = document.querySelector("[data-mermaid-ready]");
    if (!target) {
      return true;
    }
    return target.getAttribute("data-mermaid-ready") === "true";
  });
};

const waitForPlantUml = async (page) => {
  await page.waitForFunction(() => {
    const images = Array.from(document.querySelectorAll("img.plantuml"));
    if (images.length === 0) {
      return true;
    }
    return images.every((img) => img.complete && img.naturalWidth > 0);
  });
};

const launchBrowser = async (name) => {
  const browserType = browserTypes[name];
  if (!browserType) {
    throw new Error(`Unknown browser: ${name}`);
  }

  if (name === "chromium") {
    return browserType.launch({
      chromiumSandbox: false,
      args: [
        "--allow-file-access-from-files",
        "--disable-setuid-sandbox",
        "--no-sandbox"
      ]
    });
  }

  return browserType.launch();
};

const launchWithTimeout = async (name) => {
  const timeoutMs = Number(process.env.DOCS_SCREENSHOT_LAUNCH_TIMEOUT ?? 20000);
  return Promise.race([
    launchBrowser(name),
    new Promise((_, reject) => {
      setTimeout(() => {
        reject(new Error(`Timed out launching ${name} after ${timeoutMs}ms.`));
      }, timeoutMs);
    })
  ]);
};

const startStaticServer = async (debug = false) => {
  const server = http.createServer(async (req, res) => {
    try {
      const rawPath = req.url?.split("?")[0] ?? "/";
      const safePath = decodeURIComponent(rawPath);
      const normalizedPath = path.normalize(safePath);
      const relativePath = normalizedPath.replace(/^[/\\]+/, "");
      let filePath = path.join(buildDir, relativePath);

      if (fsSync.existsSync(filePath) && fsSync.statSync(filePath).isDirectory()) {
        filePath = path.join(filePath, "index.html");
      }

      if (!fsSync.existsSync(filePath)) {
        if (debug && rawPath.includes("plantuml")) {
          console.warn(`Static server 404 for ${rawPath}`);
        }
        filePath = path.join(buildDir, "index.html");
      }

      if (!filePath.startsWith(buildDir)) {
        res.writeHead(403);
        res.end("Forbidden");
        return;
      }

      const ext = path.extname(filePath);
      res.writeHead(200, {
        "Content-Type": contentTypes[ext] ?? "application/octet-stream"
      });
      if (debug && rawPath.includes("plantuml")) {
        console.warn(`Static server 200 for ${rawPath}`);
      }
      fsSync.createReadStream(filePath).pipe(res);
    } catch (error) {
      res.writeHead(500);
      res.end("Server error");
      console.warn("Static server error:", error);
    }
  });

  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });

  const address = server.address();
  const port = typeof address === "object" && address ? address.port : 4173;
  return { server, baseUrl: `http://127.0.0.1:${port}` };
};

const run = async () => {
  await fs.mkdir(outputDir, { recursive: true });
  const plantumlBlocks = await collectPlantUmlBlocks();
  if (plantumlBlocks.length > 0) {
    process.env.VITE_PLANTUML_ASSET_BASE = "plantuml";
  }
  await buildDocs();
  if (plantumlBlocks.length > 0) {
    await renderPlantUmlAssets(plantumlBlocks);
  }

  const preferredBrowser = process.env.DOCS_SCREENSHOT_BROWSER ?? "chromium";
  const debug = process.env.DOCS_SCREENSHOT_DEBUG === "1";
  let browser;

  try {
    browser = await launchWithTimeout(preferredBrowser);
  } catch (error) {
    if (preferredBrowser === "chromium" && !process.env.DOCS_SCREENSHOT_BROWSER) {
      try {
        browser = await launchWithTimeout("firefox");
      } catch (fallbackError) {
        throw new Error(
          "Failed to launch Playwright browser. Try setting DOCS_SCREENSHOT_BROWSER=firefox or ensure Chromium can run without sandboxing.",
          { cause: fallbackError }
        );
      }
    } else {
      throw error;
    }
  }

  const page = await browser.newPage({
    viewport: { width: 1400, height: 900 }
  });

  if (debug) {
    page.on("console", (message) => {
      console.log(`[browser:${message.type()}] ${message.text()}`);
    });
    page.on("pageerror", (error) => {
      console.error("[browser:error]", error);
    });
    page.on("requestfailed", (request) => {
      const failure = request.failure();
      console.warn("[browser:requestfailed]", request.url(), failure?.errorText);
    });
  }

  let server;
  let baseUrl;
  try {
    const started = await startStaticServer(debug);
    server = started.server;
    baseUrl = started.baseUrl;
  } catch (error) {
    console.warn("Static server failed, falling back to file:// URLs.", error);
    const indexPath = path.join(buildDir, "index.html");
    baseUrl = new URL(`file://${indexPath}`).href;
  }

  try {
    const indexUrl = baseUrl.endsWith(".html") ? baseUrl : `${baseUrl}/`;
    await page.goto(indexUrl, { waitUntil: "load" });
    await page.waitForSelector("[data-docs-root]");

    const screenIds = await page.$$eval("[data-screen-id]", (nodes) =>
      nodes
        .map((node) => node.getAttribute("data-screen-id"))
        .filter(Boolean)
    );

    for (const id of screenIds) {
      const screenUrl = new URL(indexUrl);
      screenUrl.searchParams.set("screen", id);
      await page.goto(screenUrl.href, { waitUntil: "load" });
      await page.waitForSelector("[data-docs-root]");
      await page.waitForFunction(
        (expected) => {
          const active = document.querySelector("[data-active-screen]");
          return active?.getAttribute("data-active-screen") === expected;
        },
        id
      );
      await waitForMermaid(page);
      await waitForPlantUml(page);

      const content = page.locator(".content");
      await content.screenshot({
        path: path.join(outputDir, `${id}.png`)
      });
    }
  } finally {
    await browser.close();
    if (server) {
      await new Promise((resolve) => server.close(resolve));
    }
  }
};

run().catch((error) => {
  console.error(error);
  process.exit(1);
});
