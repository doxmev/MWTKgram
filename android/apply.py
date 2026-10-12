#!/usr/bin/env python3
"""Applies MWTKgram changes on top of a Telegram Android checkout.

Usage: apply.py <telegram-root> <api-id> <api-hash>
"""
import json
import os
import re
import shutil
import sys

PACKAGE = "com.doxmev.mwtkgram"
APP_NAME = "MWTKgram"

root, api_id, api_hash = sys.argv[1], sys.argv[2], sys.argv[3]
here = os.path.dirname(os.path.abspath(__file__))


def path(*parts):
    return os.path.join(root, *parts)


def edit(relative, transform):
    full = path(relative)
    with open(full, encoding="utf-8") as f:
        text = f.read()
    result = transform(text)
    if result == text:
        raise SystemExit("apply.py: nothing changed in " + relative)
    with open(full, "w", encoding="utf-8") as f:
        f.write(result)


# 1. Overlay files (new and replaced sources).
overlay = os.path.join(here, "overlay")
for base, _, files in os.walk(overlay):
    for name in files:
        source = os.path.join(base, name)
        target = path(os.path.relpath(source, overlay))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(source, target)

# 2. Package, version and signing.
edit("gradle.properties", lambda t: re.sub(
    r"APP_PACKAGE=.*", "APP_PACKAGE=" + PACKAGE, t))

# 3. Only 64-bit ARM: faster builds, every modern phone.
edit("TMessagesProj_App/build.gradle", lambda t: t.replace(
    """        afat {
            ndk {
                abiFilters "armeabi-v7a", "arm64-v8a", "x86", "x86_64"
            }""",
    """        afat {
            ndk {
                abiFilters "arm64-v8a"
            }"""))

# 4. API credentials.
def build_vars(t):
    t = re.sub(r"public static int APP_ID = \d+;",
               "public static int APP_ID = %s;" % int(api_id), t)
    t = re.sub(r'public static String APP_HASH = "[0-9a-f]+";',
               'public static String APP_HASH = "%s";' % api_hash, t)
    return t
edit("TMessagesProj/src/main/java/org/telegram/messenger/BuildVars.java",
     build_vars)

# 5. Firebase config must list our package, or the build fails.
gs_path = path("TMessagesProj_App/google-services.json")
with open(gs_path, encoding="utf-8") as f:
    gs = json.load(f)
client = json.loads(json.dumps(gs["client"][0]))
client["client_info"]["android_client_info"]["package_name"] = PACKAGE
gs["client"].append(client)
with open(gs_path, "w", encoding="utf-8") as f:
    json.dump(gs, f, indent=2)

# 6. Own account type, so it can live next to the official app.
for relative in [
    "TMessagesProj/src/main/res/xml/auth.xml",
    "TMessagesProj/src/main/res/xml/sync_contacts.xml",
    "TMessagesProj/src/main/java/org/telegram/messenger/ContactsController.java",
]:
    edit(relative, lambda t: t.replace('"org.telegram.messenger"', '"%s"' % PACKAGE))

# 7. Name.
edit("TMessagesProj/src/main/res/values/strings.xml", lambda t: t.replace(
    '<string name="AppName">Telegram</string>',
    '<string name="AppName">%s</string>' % APP_NAME, 1))

# 8. Extra patches.
patches = os.path.join(here, "patches.py")
if os.path.exists(patches):
    namespace = {"edit": edit, "path": path, "re": re}
    exec(compile(open(patches, encoding="utf-8").read(), patches, "exec"), namespace)

print("apply.py: done")
