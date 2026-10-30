// =========================================================
// Pocket Pal native Mac app by Here Studios
//
// A tiny real macOS app (no osascript) written in plain C that
// talks to Cocoa through the Objective-C runtime.
//
// What it does:
//  1. Opens a window with a WKWebView that shows pet.html (the game)
//  2. Puts one outline face per pet in the Mac menu bar, each with a
//     menu (stats + quick actions) when you click it
//  3. Saves all pets to ~/Library/Application Support/PocketPal/save.json
//
// How the game talks to this app:
//  The game sets document.title to "PP|{json}". Twice a second we read
//  the title, update the menu bar faces, and write the save file.
//  On launch we inject the saved data as window.__PP_SAVE.
//  Menu actions call window.ppMenu(action, petIndex) inside the game.
// =========================================================

/* ---------- Objective-C runtime types (no SDK headers needed) ---------- */
typedef struct objc_object *id;
typedef struct objc_selector *SEL;
typedef struct objc_class *Class;
typedef id (*IMP)(id, SEL, ...);
typedef signed char BOOL;
typedef unsigned long NSUInteger;
typedef long NSInteger;
typedef struct { double x, y, w, h; } CGRect;
typedef struct { double w, h; } CGSize;
#define nil ((id)0)
#define YES ((BOOL)1)
#define NO  ((BOOL)0)

extern id   objc_getClass(const char *name);
extern SEL  sel_registerName(const char *name);
extern void objc_msgSend(void);
extern Class objc_allocateClassPair(Class super, const char *name, unsigned long extra);
extern void objc_registerClassPair(Class cls);
extern BOOL class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern void *objc_getProtocol(const char *name);
extern BOOL class_addProtocol(Class cls, void *protocol);
extern int  snprintf(char *buf, unsigned long size, const char *fmt, ...);

/* ---------- typed message helpers ----------
   On Apple Silicon every objc_msgSend call must be cast to the exact
   function type, so we keep a small helper for each shape we use. */
#define C(name)  objc_getClass(name)
#define S(sel)   sel_registerName(sel)
static id   m0 (id o, const char *s)               { return ((id(*)(id,SEL))objc_msgSend)(o, S(s)); }
static id   m1 (id o, const char *s, id a)         { return ((id(*)(id,SEL,id))objc_msgSend)(o, S(s), a); }
static id   m2 (id o, const char *s, id a, id b)   { return ((id(*)(id,SEL,id,id))objc_msgSend)(o, S(s), a, b); }
static id   mL (id o, const char *s, long a)       { return ((id(*)(id,SEL,long))objc_msgSend)(o, S(s), a); }
static id   mB (id o, const char *s, BOOL a)       { return ((id(*)(id,SEL,BOOL))objc_msgSend)(o, S(s), a); }
static id   mD (id o, const char *s, double a)     { return ((id(*)(id,SEL,double))objc_msgSend)(o, S(s), a); }
static id   mSz(id o, const char *s, CGSize a)     { return ((id(*)(id,SEL,CGSize))objc_msgSend)(o, S(s), a); }
static long rL (id o, const char *s)               { return ((long(*)(id,SEL))objc_msgSend)(o, S(s)); }
static BOOL rB1(id o, const char *s, id a)         { return ((BOOL(*)(id,SEL,id))objc_msgSend)(o, S(s), a); }
static const char *rStr(id o)                      { return ((const char*(*)(id,SEL))objc_msgSend)(o, S("UTF8String")); }

// make an NSString from C text
static id str(const char *c) { return ((id(*)(id,SEL,const char*))objc_msgSend)(C("NSString"), S("stringWithUTF8String:"), c); }
static id cat(id a, id b)    { return m1(a, "stringByAppendingString:", b); }
static id dget(id dict, const char *key) { return m1(dict, "objectForKey:", str(key)); }
static long dnum(id dict, const char *key) { id v = dget(dict, key); return v ? rL(v, "integerValue") : 0; }
static BOOL isStr(id o) { return o && rB1(o, "isKindOfClass:", C("NSString")); }

/* ---------- app state ---------- */
#define MAX_PETS 6
enum { R_TITLE, R_MOOD, R_HAPPY, R_FOOD, R_HEALTH, R_SNACK, R_PET, R_SLEEP, R_COUNT };
static id app, win, web, ctrl, statusBar;
static id savePath, lastTitle, lastSave, assetDir, rootDir, ucc, bundledGame;
static id faces;                         // NSMutableDictionary: mood -> NSImage
static id items[MAX_PETS];               // NSStatusItem per pet
static id rows[MAX_PETS][R_COUNT];       // menu rows per pet
static int petCount = -1;
static const char *ACTIONS[] = { "snack", "pet", "sleep" };

static id menuItem(const char *title, const char *action, const char *key, id target, long tag) {
  id it = ((id(*)(id,SEL,id,SEL,id))objc_msgSend)(m0(C("NSMenuItem"), "alloc"),
          S("initWithTitle:action:keyEquivalent:"), str(title), action ? S(action) : (SEL)0, str(key ? key : ""));
  if (target) m1(it, "setTarget:", target);
  mL(it, "setTag:", tag);
  return it;
}
// SF Symbols icon for menu rows (macOS 11+)
static id symbol(const char *name) {
  return m2(C("NSImage"), "imageWithSystemSymbolName:accessibilityDescription:", str(name), nil);
}
static void setImg(id item, const char *sym) { id i = symbol(sym); if (i) m1(item, "setImage:", i); }

static void showWindow(void) {
  m1(win, "makeKeyAndOrderFront:", nil);
  mB(app, "activateIgnoringOtherApps:", YES);
}

/* builds one status item + its menu for pet number i */
static void makePetItem(int i) {
  items[i] = m0(mD(statusBar, "statusItemWithLength:", -1.0), "retain");
  id menu = m0(m0(C("NSMenu"), "alloc"), "init");
  mB(menu, "setAutoenablesItems:", NO);
  long base = i * 10;
  rows[i][R_TITLE] = menuItem("", 0, 0, 0, 0);
  rows[i][R_MOOD]  = menuItem("", 0, 0, 0, 0);
  rows[i][R_HAPPY] = menuItem("", 0, 0, 0, 0);  setImg(rows[i][R_HAPPY], "face.smiling");
  rows[i][R_FOOD]  = menuItem("", 0, 0, 0, 0);  setImg(rows[i][R_FOOD], "fork.knife");
  rows[i][R_HEALTH]= menuItem("", 0, 0, 0, 0);  setImg(rows[i][R_HEALTH], "heart");
  rows[i][R_SNACK] = menuItem("Give a Snack", "menuAction:", "", ctrl, base + 0); setImg(rows[i][R_SNACK], "fork.knife");
  rows[i][R_PET]   = menuItem("Pet", "menuAction:", "", ctrl, base + 1);          setImg(rows[i][R_PET], "hand.raised");
  rows[i][R_SLEEP] = menuItem("Put to Sleep", "menuAction:", "", ctrl, base + 2); setImg(rows[i][R_SLEEP], "moon.zzz");
  for (int r = R_TITLE; r <= R_HEALTH; r++) mB(rows[i][r], "setEnabled:", NO);
  m1(menu, "addItem:", rows[i][R_TITLE]);
  m1(menu, "addItem:", rows[i][R_MOOD]);
  m1(menu, "addItem:", m0(C("NSMenuItem"), "separatorItem"));
  m1(menu, "addItem:", rows[i][R_HAPPY]);
  m1(menu, "addItem:", rows[i][R_FOOD]);
  m1(menu, "addItem:", rows[i][R_HEALTH]);
  m1(menu, "addItem:", m0(C("NSMenuItem"), "separatorItem"));
  m1(menu, "addItem:", rows[i][R_SNACK]);
  m1(menu, "addItem:", rows[i][R_PET]);
  m1(menu, "addItem:", rows[i][R_SLEEP]);
  m1(menu, "addItem:", m0(C("NSMenuItem"), "separatorItem"));
  id open = menuItem("Open Pocket Pal", "menuAction:", "o", ctrl, base + 9); setImg(open, "macwindow");
  m1(menu, "addItem:", open);
  m1(menu, "addItem:", menuItem("Quit Pocket Pal", "terminate:", "q", app, 0));
  m1(items[i], "setMenu:", menu);
}
static void setPetCount(int n) {
  if (n > MAX_PETS) n = MAX_PETS;
  if (n < 1) n = 1;                          // always keep one item (paw print when there are no pets)
  if (n == petCount) return;
  for (int i = n; i < MAX_PETS; i++) if (items[i]) { m1(statusBar, "removeStatusItem:", items[i]); m0(items[i], "release"); items[i] = nil; }
  for (int i = 0; i < n; i++) if (!items[i]) makePetItem(i);
  petCount = n;
}

// 10 block bar like ▰▰▰▰▰▰▰▱▱▱
static void bar(char *out, int size, const char *label, long v) {
  char b[64] = {0}; int p = 0, n = (int)((v + 5) / 10);
  for (int k = 0; k < 10; k++) { const char *c = k < n ? "\xE2\x96\xB0" : "\xE2\x96\xB1"; b[p++] = c[0]; b[p++] = c[1]; b[p++] = c[2]; }
  snprintf(out, size, "%s  %s  %ld%%", label, b, v);
}

/* reads the game's title message and updates everything */
static void tick(id self, SEL _cmd, id arg) {
  id t = m0(web, "title");
  if (!t || !isStr(t)) return;
  if (lastTitle && rB1(t, "isEqualToString:", lastTitle)) return;
  if (!rB1(t, "hasPrefix:", str("PP|"))) return;
  if (lastTitle) m0(lastTitle, "release");
  lastTitle = m0(t, "retain");

  id data = mL(mL(t, "substringFromIndex:", 3), "dataUsingEncoding:", 4);
  id msg = ((id(*)(id,SEL,id,NSUInteger,id*))objc_msgSend)(C("NSJSONSerialization"), S("JSONObjectWithData:options:error:"), data, 0, 0);
  if (!msg) return;

  // save file
  id save = dget(msg, "save");
  if (isStr(save) && !(lastSave && rB1(save, "isEqualToString:", lastSave))) {
    ((BOOL(*)(id,SEL,id,BOOL,NSUInteger,id*))objc_msgSend)(save, S("writeToFile:atomically:encoding:error:"), savePath, YES, 4, 0);
    if (lastSave) m0(lastSave, "release");
    lastSave = m0(save, "retain");
  }

  // one menu bar face per pet
  id pets = dget(msg, "pets");
  long n = pets ? rL(pets, "count") : 0;
  setPetCount((int)n);
  // the "Faces in the Mac menu bar" setting
  id mb = dget(msg, "menubar");
  BOOL showBar = mb ? rL(mb, "integerValue") != 0 : YES;
  for (int i = 0; i < MAX_PETS; i++) if (items[i]) mB(items[i], "setVisible:", showBar);
  if (n == 0) {
    id img = dget(faces, "none") ? m1(faces, "objectForKey:", str("none")) : nil;
    m1(m0(items[0], "button"), "setImage:", img);
    m1(m0(items[0], "button"), "setToolTip:", str("Pocket Pal"));
    m1(rows[0][R_TITLE], "setTitle:", str("Pocket Pal"));
    m1(rows[0][R_MOOD], "setTitle:", str("No pets yet. Open the app and hit Play!"));
    m1(rows[0][R_HAPPY], "setTitle:", str("Happiness"));
    m1(rows[0][R_FOOD], "setTitle:", str("Food"));
    m1(rows[0][R_HEALTH], "setTitle:", str("Health"));
    for (int r = R_SNACK; r <= R_SLEEP; r++) mB(rows[0][r], "setEnabled:", NO);
    return;
  }
  for (long i = 0; i < n && i < MAX_PETS; i++) {
    id p = mL(pets, "objectAtIndex:", i);
    id face = dget(p, "face");                // happy / meh / sad / hungry / hurt / sleep
    id img = m1(faces, "objectForKey:", face);
    id btn = m0(items[i], "button");
    if (img) { m1(btn, "setImage:", img); m1(rows[i][R_TITLE], "setImage:", img); }
    BOOL sleeping = dnum(p, "sleeping") != 0;
    char line[160];
    snprintf(line, sizeof line, "%s the %s", rStr(dget(p, "name")), rStr(dget(p, "species")));
    m1(rows[i][R_TITLE], "setTitle:", str(line));
    snprintf(line, sizeof line, "Feeling: %s", rStr(dget(p, "moodText")));
    m1(rows[i][R_MOOD], "setTitle:", str(line));
    snprintf(line, sizeof line, "%s: %s", rStr(dget(p, "name")), rStr(dget(p, "moodText")));
    m1(btn, "setToolTip:", str(line));
    bar(line, sizeof line, "Happiness", dnum(p, "happy"));  m1(rows[i][R_HAPPY], "setTitle:", str(line));
    bar(line, sizeof line, "Food        ", dnum(p, "food"));  m1(rows[i][R_FOOD], "setTitle:", str(line));
    bar(line, sizeof line, "Health      ", dnum(p, "health")); m1(rows[i][R_HEALTH], "setTitle:", str(line));
    mB(rows[i][R_SNACK], "setEnabled:", !sleeping);
    mB(rows[i][R_PET], "setEnabled:", !sleeping);
    mB(rows[i][R_SLEEP], "setEnabled:", YES);
    m1(rows[i][R_SLEEP], "setTitle:", str(sleeping ? "Wake Up" : "Put to Sleep"));
    setImg(rows[i][R_SLEEP], sleeping ? "sun.max" : "moon.zzz");
  }
}

/* a menu bar menu row was clicked: tag = petIndex * 10 + action */
static void menuAction(id self, SEL _cmd, id sender) {
  long tag = rL(sender, "tag"), pet = tag / 10, a = tag % 10;
  if (a == 9) { showWindow(); return; }
  char js[128];
  snprintf(js, sizeof js, "window.ppMenu && window.ppMenu('%s', %ld)", ACTIONS[a], pet);
  m2(web, "evaluateJavaScript:completionHandler:", str(js), nil);
}
/* ---------- FILE MANAGER: build a tidy folder and show it in Finder ----------
   PocketPal/Pets/<name>.json, PocketPal/Custom Models/<name>/..., PocketPal/Backgrounds/... */
static BOOL safeName(id n) {
  return isStr(n) && rL(n, "length") > 0 && !rB1(n, "containsString:", str("/")) && !rB1(n, "containsString:", str(".."));
}
static id fm(void) { return m0(C("NSFileManager"), "defaultManager"); }
static void mkdirs(id path) {
  ((BOOL(*)(id,SEL,id,BOOL,id,id*))objc_msgSend)(fm(), S("createDirectoryAtPath:withIntermediateDirectories:attributes:error:"), path, YES, nil, 0);
}
static void rmPath(id path) { ((BOOL(*)(id,SEL,id,id*))objc_msgSend)(fm(), S("removeItemAtPath:error:"), path, 0); }
static void writeText(id text, id path) {
  ((BOOL(*)(id,SEL,id,BOOL,NSUInteger,id*))objc_msgSend)(text, S("writeToFile:atomically:encoding:error:"), path, YES, 4, 0);
}
static void copyAsset(id key, id dest) {
  if (!safeName(key)) return;
  ((BOOL(*)(id,SEL,id,id,id*))objc_msgSend)(fm(), S("copyItemAtPath:toPath:error:"), cat(cat(assetDir, str("/")), key), dest, 0);
}
static id sub(id base, const char *name) { return cat(cat(base, str("/")), str(name)); }
static id join(id base, id name) { return cat(cat(base, str("/")), name); }
static void openFolder(id body) {
  id petsDir = sub(rootDir, "Pets"), modelsDir = sub(rootDir, "Custom Models"), bgDir = sub(rootDir, "Backgrounds");
  // rebuild the three folders from scratch so they always match the game
  rmPath(petsDir); rmPath(modelsDir); rmPath(bgDir);
  mkdirs(petsDir); mkdirs(modelsDir); mkdirs(bgDir);
  id list = dget(body, "pets");
  for (long i = 0; list && i < rL(list, "count"); i++) {
    id p = mL(list, "objectAtIndex:", i), n = dget(p, "name"), t = dget(p, "text");
    if (safeName(n) && isStr(t)) writeText(t, join(petsDir, n));
  }
  list = dget(body, "models");
  for (long i = 0; list && i < rL(list, "count"); i++) {
    id m = mL(list, "objectAtIndex:", i), d = dget(m, "dir");
    if (!safeName(d)) continue;
    id dir = join(modelsDir, d); mkdirs(dir);
    id files = dget(m, "files");
    for (long k = 0; files && k < rL(files, "count"); k++) {
      id f = mL(files, "objectAtIndex:", k), n = dget(f, "name");
      if (safeName(n)) copyAsset(dget(f, "key"), join(dir, n));
    }
    id pack = dget(m, "pack");
    if (isStr(pack)) writeText(pack, sub(dir, "pet-pack.json"));
  }
  list = dget(body, "bgs");
  for (long i = 0; list && i < rL(list, "count"); i++) {
    id b = mL(list, "objectAtIndex:", i), n = dget(b, "name");
    if (safeName(n)) copyAsset(dget(b, "key"), join(bgDir, n));
  }
  writeText(str("Pocket Pal folder\n\n"
                "Pets: one file per pet with its stats and looks.\n"
                "Custom Models: the files you uploaded for each custom character, plus a pet-pack.json.\n"
                "  To use one again, drop everything in its folder onto the Custom screen in Pocket Pal.\n"
                "Backgrounds: pictures you uploaded in the Pet Shop.\n\n"
                "These folders are copies, rebuilt each time you press Open folder.\n"
                "save.json and the assets folder are what the game really uses, so leave those alone.\n"),
            sub(rootDir, "Read Me.txt"));
  m1(m0(C("NSWorkspace"), "sharedWorkspace"), "openURL:", m1(C("NSURL"), "fileURLWithPath:", rootDir));
}

/* ---------- AUTO UPDATE ----------
   The game downloads a newer pet.html from GitHub and hands it to us.
   We keep it in PocketPal/game/pet.html and load that from now on. */
static id downloadedGame(void) { return sub(rootDir, "game/pet.html"); }
static void loadGame(void) {
  // the save is handed to the game before its own code runs
  m0(ucc, "removeAllUserScripts");
  id boot = cat(cat(str("window.__PP_SAVE = "), lastSave ? lastSave : str("null")), str(";"));
  m1(ucc, "addUserScript:", ((id(*)(id,SEL,id,long,BOOL))objc_msgSend)(m0(C("WKUserScript"), "alloc"),
        S("initWithSource:injectionTime:forMainFrameOnly:"), boot, 0, YES));
  BOOL hasUpdate = ((BOOL(*)(id,SEL,id))objc_msgSend)(fm(), S("fileExistsAtPath:"), downloadedGame());
  id url = m1(C("NSURL"), "fileURLWithPath:", hasUpdate ? downloadedGame() : bundledGame);
  m2(web, "loadFileURL:allowingReadAccessToURL:", url, m0(url, "URLByDeletingLastPathComponent"));
}
static void installUpdate(id body) {
  id html = dget(body, "html");
  if (!isStr(html) || !rB1(html, "containsString:", str("POCKET_PAL_GAME"))) return;
  tick(ctrl, 0, nil);                          // grab the newest save before reloading
  mkdirs(sub(rootDir, "game"));
  writeText(html, downloadedGame());
  lastTitle = nil;
  loadGame();
}

/* ---------- files uploaded in the game (custom pets, backgrounds) ----------
   The game sends { cmd:'put'|'get'|'del', key, b64, id } and we keep each file in
   ~/Library/Application Support/PocketPal/assets/<key> */
static void onMessage(id self, SEL _cmd, id ucc, id message) {
  id body = m0(message, "body");
  if (!body || !rB1(body, "isKindOfClass:", C("NSDictionary"))) return;
  id cmd = dget(body, "cmd"), key = dget(body, "key");
  if (isStr(cmd) && rB1(cmd, "isEqualToString:", str("open"))) { openFolder(body); return; }
  if (isStr(cmd) && rB1(cmd, "isEqualToString:", str("update"))) { installUpdate(body); return; }
  if (!isStr(cmd) || !isStr(key) || rL(key, "length") == 0) return;
  // keys are plain file names made by the game, never paths
  if (rB1(key, "containsString:", str("/")) || rB1(key, "containsString:", str(".."))) return;
  id path = cat(cat(assetDir, str("/")), key);
  if (rB1(cmd, "isEqualToString:", str("put"))) {
    id b64 = dget(body, "b64");
    if (!isStr(b64)) return;
    id data = ((id(*)(id,SEL,id,NSUInteger))objc_msgSend)(m0(C("NSData"), "alloc"), S("initWithBase64EncodedString:options:"), b64, 1);
    if (data) ((BOOL(*)(id,SEL,id,BOOL))objc_msgSend)(data, S("writeToFile:atomically:"), path, YES);
  } else if (rB1(cmd, "isEqualToString:", str("get"))) {
    id data = m1(C("NSData"), "dataWithContentsOfFile:", path);
    id b64 = data ? mL(data, "base64EncodedStringWithOptions:", 0) : str("");
    id rid = dget(body, "id");
    char head[64];
    snprintf(head, sizeof head, "window.__ppGot(%ld,'", rid ? rL(rid, "integerValue") : 0L);
    m2(web, "evaluateJavaScript:completionHandler:", cat(cat(str(head), b64), str("')")), nil);
  } else if (rB1(cmd, "isEqualToString:", str("del"))) {
    ((BOOL(*)(id,SEL,id,id*))objc_msgSend)(m0(C("NSFileManager"), "defaultManager"), S("removeItemAtPath:error:"), path, 0);
  }
}

/* ---------- <input type=file> in the game opens a normal Mac file picker ---------- */
struct Block { void *isa; int flags; int reserved; void (*invoke)(void *, ...); };
static void openPanel(id self, SEL _cmd, id webView, id params, id frame, void *handler) {
  id panel = m0(C("NSOpenPanel"), "openPanel");
  mB(panel, "setCanChooseFiles:", YES);
  mB(panel, "setCanChooseDirectories:", NO);
  mB(panel, "setAllowsMultipleSelection:", ((BOOL(*)(id,SEL))objc_msgSend)(params, S("allowsMultipleSelection")));
  long result = rL(panel, "runModal");              // 1 = the player clicked Open
  id urls = result == 1 ? m0(panel, "URLs") : nil;
  struct Block *b = (struct Block *)handler;
  ((void(*)(void *, id))b->invoke)(b, urls);
}

static BOOL keepRunning(id self, SEL _cmd, id sender) { return NO; }       // closing the window keeps the menu bar faces
static BOOL reopen(id self, SEL _cmd, id sender, BOOL visible) { showWindow(); return YES; }
static void willQuit(id self, SEL _cmd, id note) { lastTitle = nil; tick(self, _cmd, nil); }

static id menuWith(const char *title, id *list, int n) {
  id holder = m0(m0(C("NSMenuItem"), "alloc"), "init");
  id m = m1(m0(C("NSMenu"), "alloc"), "initWithTitle:", str(title));
  for (int i = 0; i < n; i++) m1(m, "addItem:", list[i]);
  m1(holder, "setSubmenu:", m);
  return holder;
}

int main(void) {
  m0(m0(C("NSAutoreleasePool"), "alloc"), "init");
  app = m0(C("NSApplication"), "sharedApplication");
  mL(app, "setActivationPolicy:", 0);               // regular app with Dock icon

  // controller class: timer, menu actions, app delegate
  Class cls = objc_allocateClassPair((Class)C("NSObject"), "PocketPalController", 0);
  class_addMethod(cls, S("tick:"), (IMP)tick, "v@:@");
  class_addMethod(cls, S("menuAction:"), (IMP)menuAction, "v@:@");
  class_addMethod(cls, S("applicationShouldTerminateAfterLastWindowClosed:"), (IMP)keepRunning, "c@:@");
  class_addMethod(cls, S("applicationShouldHandleReopen:hasVisibleWindows:"), (IMP)reopen, "c@:@c");
  class_addMethod(cls, S("applicationWillTerminate:"), (IMP)willQuit, "v@:@");
  class_addMethod(cls, S("userContentController:didReceiveScriptMessage:"), (IMP)onMessage, "v@:@@");
  class_addMethod(cls, S("webView:runOpenPanelWithParameters:initiatedByFrame:completionHandler:"), (IMP)openPanel, "v@:@@@@?");
  void *proto = objc_getProtocol("WKScriptMessageHandler"); if (proto) class_addProtocol(cls, proto);
  proto = objc_getProtocol("WKUIDelegate"); if (proto) class_addProtocol(cls, proto);
  objc_registerClassPair(cls);
  ctrl = m0(m0((id)cls, "alloc"), "init");
  m1(app, "setDelegate:", ctrl);

  // paths
  id res = m0(m0(C("NSBundle"), "mainBundle"), "resourcePath");
  id dir = m0(str("~/Library/Application Support/PocketPal"), "stringByExpandingTildeInPath");
  ((BOOL(*)(id,SEL,id,BOOL,id,id*))objc_msgSend)(m0(C("NSFileManager"), "defaultManager"),
      S("createDirectoryAtPath:withIntermediateDirectories:attributes:error:"), dir, YES, nil, 0);
  savePath = m0(cat(dir, str("/save.json")), "retain");
  rootDir = m0(dir, "retain");
  assetDir = m0(cat(dir, str("/assets")), "retain");
  ((BOOL(*)(id,SEL,id,BOOL,id,id*))objc_msgSend)(m0(C("NSFileManager"), "defaultManager"),
      S("createDirectoryAtPath:withIntermediateDirectories:attributes:error:"), assetDir, YES, nil, 0);

  // read the old save and make sure it is valid JSON
  id saveText = str("null");
  id raw = ((id(*)(id,SEL,id,NSUInteger,id*))objc_msgSend)(C("NSString"), S("stringWithContentsOfFile:encoding:error:"), savePath, 4, 0);
  if (raw) {
    id ok = ((id(*)(id,SEL,id,NSUInteger,id*))objc_msgSend)(C("NSJSONSerialization"), S("JSONObjectWithData:options:error:"),
            mL(raw, "dataUsingEncoding:", 4), 4 /* allow fragments */, 0);
    if (ok) saveText = raw;
  }
  lastSave = m0(saveText, "retain");

  // menu bar face images (template = macOS colors them for light/dark bars; hurt stays red)
  faces = m0(m0(C("NSMutableDictionary"), "alloc"), "init");
  const char *names[] = { "happy", "meh", "sad", "hungry", "hurt", "sleep", "none" };
  for (int i = 0; i < 7; i++) {
    char p[64]; snprintf(p, sizeof p, "/menubar/face_%s.png", names[i]);
    id img = m1(m0(C("NSImage"), "alloc"), "initWithContentsOfFile:", cat(res, str(p)));
    if (!img) continue;
    mSz(img, "setSize:", (CGSize){18, 18});
    mB(img, "setTemplate:", i != 4);
    m2(faces, "setObject:forKey:", img, str(names[i]));
  }

  // main menu (Cmd+Q, Cmd+W, copy and paste)
  id mainMenu = m0(m0(C("NSMenu"), "alloc"), "init");
  id appItems[] = { menuItem("Hide Pocket Pal", "hide:", "h", 0, 0), m0(C("NSMenuItem"), "separatorItem"), menuItem("Quit Pocket Pal", "terminate:", "q", 0, 0) };
  id editItems[] = { menuItem("Cut", "cut:", "x", 0, 0), menuItem("Copy", "copy:", "c", 0, 0), menuItem("Paste", "paste:", "v", 0, 0), menuItem("Select All", "selectAll:", "a", 0, 0) };
  id winItems[] = { menuItem("Close Window", "performClose:", "w", 0, 0), menuItem("Minimize", "performMiniaturize:", "m", 0, 0) };
  m1(mainMenu, "addItem:", menuWith("Pocket Pal", appItems, 3));
  m1(mainMenu, "addItem:", menuWith("Edit", editItems, 4));
  m1(mainMenu, "addItem:", menuWith("Window", winItems, 2));
  m1(app, "setMainMenu:", mainMenu);

  // the window with the game inside
  CGRect rect = { 0, 0, 940, 680 };
  win = ((id(*)(id,SEL,CGRect,NSUInteger,NSUInteger,BOOL))objc_msgSend)(m0(C("NSWindow"), "alloc"),
        S("initWithContentRect:styleMask:backing:defer:"), rect, 15, 2, NO);
  mB(win, "setReleasedWhenClosed:", NO);
  m1(win, "setTitle:", str("Pocket Pal"));
  mSz(win, "setMinSize:", (CGSize){720, 560});
  m1(win, "setBackgroundColor:", ((id(*)(id,SEL,double,double,double,double))objc_msgSend)(C("NSColor"),
        S("colorWithSRGBRed:green:blue:alpha:"), 0.05, 0.04, 0.15, 1.0));
  m0(win, "center");

  id config = m0(m0(C("WKWebViewConfiguration"), "alloc"), "init");
  ucc = m0(m0(config, "userContentController"), "retain");
  m2(ucc, "addScriptMessageHandler:name:", ctrl, str("pp"));
  web = ((id(*)(id,SEL,CGRect,id))objc_msgSend)(m0(C("WKWebView"), "alloc"), S("initWithFrame:configuration:"), rect, config);
  mL(web, "setAutoresizingMask:", 18);
  m1(web, "setUIDelegate:", ctrl);
  bundledGame = m0(cat(res, str("/pet.html")), "retain");
  loadGame();                                  // uses a downloaded update if there is one
  m1(win, "setContentView:", web);

  // menu bar
  statusBar = m0(C("NSStatusBar"), "systemStatusBar");
  setPetCount(1);
  m1(m0(items[0], "button"), "setImage:", m1(faces, "objectForKey:", str("none")));

  // check the game twice a second
  ((id(*)(id,SEL,double,id,SEL,id,BOOL))objc_msgSend)(C("NSTimer"),
      S("scheduledTimerWithTimeInterval:target:selector:userInfo:repeats:"), 0.5, ctrl, S("tick:"), nil, YES);

  showWindow();
  m0(app, "run");
  return 0;
}
