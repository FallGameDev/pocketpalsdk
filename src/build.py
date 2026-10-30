# =========================================================
# Builds game/pet.html: one file with the game, three.js and the
# cat + dog models all inlined, so it works offline.
#
#   python3 src/build.py          build with the current version
#   python3 src/build.py --bump   add 1 to the version first (do this for a release,
#                                 the Mac app only downloads a NEWER version number)
# =========================================================
import base64, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VERSION_FILE = os.path.join(ROOT, 'game', 'version.json')

info = json.load(open(VERSION_FILE))
if '--bump' in sys.argv:
    info['version'] += 1
    json.dump(info, open(VERSION_FILE, 'w'), indent=2)
version = int(info['version'])

src = open(os.path.join(HERE, 'pet.src.html'), encoding='utf-8').read()

# three.js r147 and its loaders (MIT license, see libs/three-LICENSE.txt)
libs = ['three.min.js', 'fflate.min.js', 'FBXLoader.js', 'GLTFLoader.js', 'OBJLoader.js', 'SkeletonUtils.js']
code = '\n;\n'.join(open(os.path.join(HERE, 'libs', n), encoding='utf-8').read() for n in libs)
assert '</script' not in code.lower()

# the built-in pets, stored as base64 text
b64 = lambda n: base64.b64encode(open(os.path.join(HERE, 'assets', n), 'rb').read()).decode()
assets = json.dumps({'cat': b64('cat.fbx'), 'catTex': b64('cat_tex.jpg'), 'dog': b64('dog.glb')})

out = (src.replace('/*__LIBS__*/', code, 1)
          .replace('/*__ASSETS__*/', assets, 1)
          .replace('/*__VERSION__*/0', str(version), 1))
assert f'GAME_VERSION = {version};' in out, 'version marker missing in pet.src.html'
dest = os.path.join(ROOT, 'game', 'pet.html')
open(dest, 'w', encoding='utf-8').write(out)
print(f'built game/pet.html version {version} ({len(out)//1024} KB)')
