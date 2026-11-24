// ModernExtractDialog.h - Bandizip-style native Shell Extract Path dialog

#ifndef ZIP7_INC_MODERN_EXTRACT_DIALOG_H
#define ZIP7_INC_MODERN_EXTRACT_DIALOG_H

#include "../../../Common/MyWindows.h"
#include "../../../Common/MyString.h"

namespace NModernUI {

enum EExtractDialogResult
{
  kExtractCancel = 0,
  kExtractOk = 1,
  kExtractAdvanced = 2
};

struct CModernExtractOptions
{
  // Inputs
  UString TargetDir;
  UString ArchivePath;       // Used to compute archive-name subfolder
  bool CanSelectFiles;       // Enable "Selected files" radio (true inside 7zFM)
  bool ExtractSelectedOnly;  // Initial & output choice: true = selected, false = all
  bool ShowAdvancedLink;     // Show "Advanced..." link to stock 7-Zip dialog

  // Outputs (and persisted when RememberSettings is checked)
  bool ExtractToArchiveFolder;
  bool DeleteArchiveAfterExtract;
  bool RememberSettings;

  CModernExtractOptions():
    CanSelectFiles(false),
    ExtractSelectedOnly(false),
    ShowAdvancedLink(true),
    ExtractToArchiveFolder(false),
    DeleteArchiveAfterExtract(false),
    RememberSettings(true)
  {}
};

// Shows the Bandizip-style "Choose Extract Path" modal dialog.
EExtractDialogResult ShowModernExtractDialog(HWND hwndParent, CModernExtractOptions &opt);

// Appends the archive's base name (handling .tar.gz/.tar.xz/.tar.zst/.tar.bz2)
// as a subfolder of dirPath, avoiding double-nesting if already ending with it.
void AppendArchiveNameSubfolder(UString &dirPath, const UString &archivePath);

// Safely moves a file to the Windows Recycle Bin (no confirmation, silent).
bool MoveFileToRecycleBin(const UString &filePath);

} // namespace NModernUI

#endif
