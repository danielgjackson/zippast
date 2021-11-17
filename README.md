# ZIP-Past

Masks a `.zip` file by making sure the markers are not found immediately at the start or end of a file; also optionally completely contains it within a valid container, such as a `.bmp` image or a `.wav` audio file. <!-- Additionally, it can use a less common format (extended local headers) for each file. -->

Why? For example, those times when you really need to attach an executable (in a `.zip` file) to an email but the mail server blocks executables (even in archives). This tool may let you bypass this restriction, and the recipient needs only to rename the file `*.zip` to use the archive.

As of the time of writing, *Gmail* appears to consider a file a ZIP archive if it has *any* of these features:

  * a file extension `.zip`; or
  * immediately begins with a `PK...` ZIP header; or
  * has an end-of-central-directory section within the last 8kB of the file.

So, it is enough to get past Gmail's content filters if a .ZIP file has (all of):

  * a file extension other than `.zip`; and
  * one or more initial bytes so that first ZIP header does not start at immediately at the beginning of a file; and
  * an end of file comment of at least 8171 bytes so that the end-of-central-directory section does not begin within the last 8kB.

This tool does this for you.

**IMPORTANT:** Please do not use this tool for evil!

Browser-based online version of the tool:

* [danielgjackson.github.io/zippast](https://danielgjackson.github.io/zippast/)

Command-line usage:

```bash
zippast inputfile
```

...where the input file can be a `.zip` (it will be `.zip` stored if it's not already a `.zip` file), and the output file is of the same name with the extension `.zip-email`. The recipient must rename the file to `.zip` to access the contents as before.

Use the flag `-mode:bmp` to create a valid (but nonsense) bitmap image.  

Use the flag `-mode:wav` to create a valid (but nonsense) wave audio file.

Use the option `-out output.ext` to override the output file name.
