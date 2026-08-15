# Project images

Images used in the documentation and in presentations: installed hardware, the panel
UI, app screens, wiring diagrams, measurement charts.

## Naming convention

English, kebab-case, **describe the content, not the camera that took it**:

```
hardware-installation.png    ✅  the name alone tells you what is in the shot
panel-humidifier-tab.png     ✅
app-control-screen.png       ✅
20260727_104251.png          ❌  camera-generated name, says nothing
IMG_1234.jpg                 ❌
```

The reason is not aesthetics: documents embed images by path, and six months from now
nobody will remember what `20260727_104251.png` was a picture of, so nobody can replace
the right one.

## Embedding in a document

Relative path from the `.md` file you are writing:

```markdown
![The six devices after installation](images/hardware-installation.png)
```

## Image size

Compress before committing. A phone photo is usually 2–5 MB, while a document only
needs about 1600 px across. This repo has already ballooned once because of binary
files nobody used, and git **never forgets** a file once it is committed — deleting it
later does not make the history any lighter.

```bash
# ImageMagick
magick input.jpg -resize 1600x -quality 82 docs/images/descriptive-name.png
```

## Current contents

| File | Contents |
|---|---|
| `hardware-installation.png` | Photo of the hardware during installation |
