/*++
Copyright (c) 2007 - 2009, Intel Corporation
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

Module Name:

  Setup.c

Abstract:

  Entry and initialization module for the browser.

--*/

#include "Setup.h"
#include "Ui.h"

SETUP_DRIVER_PRIVATE_DATA  mPrivateData = {
  SETUP_DRIVER_SIGNATURE,
  NULL,
  {
    SendForm,
    BrowserCallback
  },
  {
    VSPrint
  }
};

EFI_HII_DATABASE_PROTOCOL         *mHiiDatabase;
EFI_HII_STRING_PROTOCOL           *mHiiString;
EFI_HII_CONFIG_ROUTING_PROTOCOL   *mHiiConfigRouting;

UINTN           gBrowserContextCount = 0;
EFI_LIST_ENTRY  gBrowserContextList = INITIALIZE_LIST_HEAD_VARIABLE (gBrowserContextList);

BANNER_DATA           *gBannerData;
EFI_HII_HANDLE        gFrontPageHandle;
UINTN                 gClassOfVfr;
UINTN                 gFunctionKeySetting;
BOOLEAN               gResetRequired;
BOOLEAN               gNvUpdateRequired;
EFI_HII_HANDLE        gHiiHandle;
UINT16                gDirection;
EFI_SCREEN_DESCRIPTOR gScreenDimensions;

//
// Browser Global Strings
//
CHAR16            *gFunctionNineString;
CHAR16            *gFunctionTenString;
CHAR16            *gEnterString;
CHAR16            *gEnterCommitString;
CHAR16            *gEnterEscapeString;
CHAR16            *gEscapeString;
CHAR16            *gSaveFailed;
CHAR16            *gMoveHighlight;
CHAR16            *gMakeSelection;
CHAR16            *gDecNumericInput;
CHAR16            *gHexNumericInput;
CHAR16            *gToggleCheckBox;
CHAR16            *gPromptForData;
CHAR16            *gPromptForPassword;
CHAR16            *gPromptForNewPassword;
CHAR16            *gConfirmPassword;
CHAR16            *gConfirmError;
CHAR16            *gPassowordInvalid;
CHAR16            *gPressEnter;
CHAR16            *gEmptyString;
CHAR16            *gAreYouSure;
CHAR16            *gYesResponse;
CHAR16            *gNoResponse;
CHAR16            *gMiniString;
CHAR16            *gPlusString;
CHAR16            *gMinusString;
CHAR16            *gAdjustNumber;
CHAR16            *gSaveChanges;
CHAR16            *gOptionMismatch;

CHAR16            gPromptBlockWidth;
CHAR16            gOptionBlockWidth;
CHAR16            gHelpBlockWidth;

EFI_GUID  gZeroGuid = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
EFI_GUID  gSetupBrowserGuid = {
  0xab368524, 0xb60c, 0x495b, 0xa0, 0x9, 0x12, 0xe8, 0x5b, 0x1a, 0xea, 0x32
};
EFI_GUID  gPlatformSetupClassGuid = EFI_HII_PLATFORM_SETUP_FORMSET_GUID;
EFI_GUID  gFrontPageClassGuid = EFI_HII_FRONT_PAGE_CLASS_GUID;

FORM_BROWSER_FORMSET  *gOldFormSet;

FUNCTIION_KEY_SETTING gFunctionKeySettingTable[] = {
  //
  // Boot Manager
  //
  {
    {
      0x847bc3fe,
      0xb974,
      0x446d,
      0x94,
      0x49,
      0x5a,
      0xd5,
      0x41,
      0x2e,
      0x99,
      0x3b
    },
    NONE_FUNCTION_KEY_SETTING
  },
  //
  // Device Manager
  //
  {
    {
      0x3ebfa8e6,
      0x511d,
      0x4b5b,
      0xa9,
      0x5f,
      0xfb,
      0x38,
      0x26,
      0xf,
      0x1c,
      0x27
    },
    NONE_FUNCTION_KEY_SETTING
  },
  //
  // BMM FormSet.
  //
  {
    {
      0x642237c7,
      0x35d4,
      0x472d,
      0x83,
      0x65,
      0x12,
      0xe0,
      0xcc,
      0xf2,
      0x7a,
      0x22
    },
    NONE_FUNCTION_KEY_SETTING
  },
  //
  // BMM File Explorer FormSet.
  //
  {
    {
      0x1f2d63e1,
      0xfebd,
      0x4dc7,
      0x9c,
      0xc5,
      0xba,
      0x2b,
      0x1c,
      0xef,
      0x9c,
      0x5b
    },
    NONE_FUNCTION_KEY_SETTING
  },
};

EFI_DRIVER_ENTRY_POINT (InitializeSetup)

EFI_STATUS
EFIAPI
SendForm (
  IN  CONST EFI_FORM_BROWSER2_PROTOCOL *This,
  IN  EFI_HII_HANDLE                   *Handles,
  IN  UINTN                            HandleCount,
  IN  EFI_GUID                         *FormSetGuid, OPTIONAL
  IN  UINT16                           FormId, OPTIONAL
  IN  CONST EFI_SCREEN_DESCRIPTOR      *ScreenDimensions, OPTIONAL
  OUT EFI_BROWSER_ACTION_REQUEST       *ActionRequest  OPTIONAL
  )
/*++

Routine Description:
  This is the routine which an external caller uses to direct the browser
  where to obtain it's information.

Arguments:
  This            - The Form Browser protocol instanse.
  Handles         - A pointer to an array of Handles.  If HandleCount > 1 we
                    display a list of the formsets for the handles specified.
  HandleCount     - The number of Handles specified in Handle.
  FormSetGuid     - This field points to the EFI_GUID which must match the Guid
                    field in the EFI_IFR_FORM_SET op-code for the specified
                    forms-based package. If FormSetGuid is NULL, then this
                    function will display the first found forms package.
  FormId          - This field specifies which EFI_IFR_FORM to render as the first
                    displayable page. If this field has a value of 0x0000, then
                    the forms browser will render the specified forms in their encoded order.
  ScreenDimenions - This allows the browser to be called so that it occupies a
                    portion of the physical screen instead of dynamically determining the screen dimensions.
  ActionRequest   - Points to the action recommended by the form.

Returns:
  EFI_SUCCESS           -  The function completed successfully.
  EFI_INVALID_PARAMETER -  One of the parameters has an invalid value.
  EFI_NOT_FOUND         -  No valid forms could be found to display.

--*/
{
  EFI_STATUS            Status;
  UI_MENU_SELECTION     *Selection;
  UINTN                 Index;
  FORM_BROWSER_FORMSET  *FormSet;

  //
  // Save globals used by SendForm()
  //
  SaveBrowserContext ();

  Status = EFI_SUCCESS;
  EfiZeroMem (&gScreenDimensions, sizeof (EFI_SCREEN_DESCRIPTOR));

  //
  // Seed the dimensions in the global
  //
  gST->ConOut->QueryMode (
                 gST->ConOut,
                 gST->ConOut->Mode->Mode,
                 &gScreenDimensions.RightColumn,
                 &gScreenDimensions.BottomRow
                 );

  if (ScreenDimensions != NULL) {
    //
    // Check local dimension vs. global dimension.
    //
    if ((gScreenDimensions.RightColumn < ScreenDimensions->RightColumn) ||
        (gScreenDimensions.BottomRow < ScreenDimensions->BottomRow)
        ) {
      Status = EFI_INVALID_PARAMETER;
      goto Done;
    } else {
      //
      // Local dimension validation.
      //
      if ((ScreenDimensions->RightColumn > ScreenDimensions->LeftColumn) &&
          (ScreenDimensions->BottomRow > ScreenDimensions->TopRow) &&
          ((ScreenDimensions->RightColumn - ScreenDimensions->LeftColumn) > 2) &&
          (
            (ScreenDimensions->BottomRow - ScreenDimensions->TopRow) > STATUS_BAR_HEIGHT +
            SCROLL_ARROW_HEIGHT *
            2 +
            FRONT_PAGE_HEADER_HEIGHT +
            FOOTER_HEIGHT +
            1
          )
        ) {
        EfiCopyMem (&gScreenDimensions, (VOID *) ScreenDimensions, sizeof (EFI_SCREEN_DESCRIPTOR));
      } else {
        Status = EFI_INVALID_PARAMETER;
        goto Done;
      }
    }
  }

  gOptionBlockWidth = (CHAR16) ((gScreenDimensions.RightColumn - gScreenDimensions.LeftColumn) / 3);
  gHelpBlockWidth   = gOptionBlockWidth;
  gPromptBlockWidth = gOptionBlockWidth;

  //
  // Initialize the strings for the browser, upon exit of the browser, the strings will be freed
  //
  InitializeBrowserStrings ();

  gFunctionKeySetting = DEFAULT_FUNCTION_KEY_SETTING;

  //
  // Ensure we are in Text mode
  //
  gST->ConOut->SetAttribute (gST->ConOut, EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));
  DisableQuietBoot ();

  for (Index = 0; Index < HandleCount; Index++) {
    Selection = EfiLibAllocateZeroPool (sizeof (UI_MENU_SELECTION));
    ASSERT (Selection != NULL);

    Selection->Handle = Handles[Index];
    if (FormSetGuid != NULL) {
      EfiCopyMem (&Selection->FormSetGuid, FormSetGuid, sizeof (EFI_GUID));
      Selection->FormId = FormId;
    }

    gOldFormSet = NULL;
    gNvUpdateRequired = FALSE;

    do {
      FormSet = EfiLibAllocateZeroPool (sizeof (FORM_BROWSER_FORMSET));
      ASSERT (FormSet != NULL);

      //
      // Initialize internal data structures of FormSet
      //
      Status = InitializeFormSet (Selection->Handle, &Selection->FormSetGuid, FormSet);
      if (EFI_ERROR (Status) || IsListEmpty (&FormSet->FormListHead)) {
        DestroyFormSet (FormSet);
        break;
      }
      Selection->FormSet = FormSet;

      //
      // Initialize current settings of Questions in this FormSet
      //
      Status = InitializeCurrentSetting (FormSet);
      if (EFI_ERROR (Status)) {
        DestroyFormSet (FormSet);
        break;
      }

      //
      // Display this formset
      //
      gCurrentSelection = Selection;

      Status = SetupBrowser (Selection);

      gCurrentSelection = NULL;
      if (gOldFormSet != NULL) {
        DestroyFormSet (gOldFormSet);
      }
      gOldFormSet = FormSet;

      if (EFI_ERROR (Status)) {
        break;
      }

    } while (Selection->Action == UI_ACTION_REFRESH_FORMSET);

    if (gOldFormSet != NULL) {
      DestroyFormSet (gOldFormSet);
      gOldFormSet = NULL;
    }

    gBS->FreePool (Selection);
  }

  if (ActionRequest != NULL) {
    *ActionRequest = EFI_BROWSER_ACTION_REQUEST_NONE;
    if (gResetRequired) {
      *ActionRequest = EFI_BROWSER_ACTION_REQUEST_RESET;
    }
  }

  FreeBrowserStrings ();

  gST->ConOut->SetAttribute (gST->ConOut, EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));
  gST->ConOut->ClearScreen (gST->ConOut);

Done:
  //
  // Restore globals used by SendForm()
  //
  RestoreBrowserContext ();

  return Status;
}

EFI_STATUS
EFIAPI
BrowserCallback (
  IN CONST EFI_FORM_BROWSER2_PROTOCOL  *This,
  IN OUT UINTN                         *ResultsDataSize,
  IN OUT EFI_STRING                    ResultsData,
  IN BOOLEAN                           RetrieveData,
  IN CONST EFI_GUID                    *VariableGuid, OPTIONAL
  IN CONST CHAR16                      *VariableName  OPTIONAL
  )
/*++

Routine Description:
  This function is called by a callback handler to retrieve uncommitted state
  data from the browser.

Arguments:
  This            - A pointer to the EFI_FORM_BROWSER2_PROTOCOL instance.
  ResultsDataSize - A pointer to the size of the buffer associated with ResultsData.
  ResultsData     - A string returned from an IFR browser or equivalent.
                    The results string will have no routing information in them.
  RetrieveData    - A BOOLEAN field which allows an agent to retrieve (if RetrieveData = TRUE)
                    data from the uncommitted browser state information or set
                    (if RetrieveData = FALSE) data in the uncommitted browser state information.
  VariableGuid    - An optional field to indicate the target variable GUID name to use.
  VariableName    - An optional field to indicate the target human-readable variable name.

Returns:
  EFI_SUCCESS           -  The results have been distributed or are awaiting distribution.
  EFI_BUFFER_TOO_SMALL  -  The ResultsDataSize specified was too small to contain the results data.

--*/
{
  EFI_STATUS            Status;
  EFI_LIST_ENTRY        *Link;
  FORMSET_STORAGE       *Storage;
  FORM_BROWSER_FORMSET  *FormSet;
  BOOLEAN               Found;
  CHAR16                *ConfigResp;
  CHAR16                *StrPtr;
  UINTN                 BufferSize;

  if (ResultsDataSize == NULL || ResultsData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (gCurrentSelection == NULL) {
    return EFI_NOT_READY;
  }

  Storage = NULL;
  ConfigResp = NULL;
  FormSet = gCurrentSelection->FormSet;

  //
  // Find target storage
  //
  Link = GetFirstNode (&FormSet->StorageListHead);
  if (IsNull (&FormSet->StorageListHead, Link)) {
    return EFI_UNSUPPORTED;
  }

  if (VariableGuid != NULL) {
    //
    // Try to find target storage
    //
    Found = FALSE;
    while (!IsNull (&FormSet->StorageListHead, Link)) {
      Storage = FORMSET_STORAGE_FROM_LINK (Link);
      Link = GetNextNode (&FormSet->StorageListHead, Link);

      if (EfiCompareGuid (&Storage->Guid, (EFI_GUID *) VariableGuid)) {
        if (Storage->Type == EFI_HII_VARSTORE_BUFFER) {
          //
          // Buffer storage require both GUID and Name
          //
          if (VariableName == NULL) {
            return EFI_NOT_FOUND;
          }

          if (EfiStrCmp (Storage->Name, (CHAR16 *) VariableName) != 0) {
            continue;
          }
        }
        Found = TRUE;
        break;
      }
    }

    if (!Found) {
      return EFI_NOT_FOUND;
    }
  } else {
    //
    // GUID/Name is not specified, take the first storage in FormSet
    //
    Storage = FORMSET_STORAGE_FROM_LINK (Link);
  }

  if (RetrieveData) {
    //
    // Skip if there is no RequestElement
    //
    if (Storage->ElementCount == 0) {
      return EFI_SUCCESS;
    }

    //
    // Generate <ConfigResp>
    //
    Status = StorageToConfigResp (Storage, &ConfigResp);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Skip <ConfigHdr> and '&' to point to <ConfigBody>
    //
    StrPtr = ConfigResp + EfiStrLen (Storage->ConfigHdr) + 1;

    BufferSize = EfiStrSize (StrPtr);
    if (*ResultsDataSize < BufferSize) {
      *ResultsDataSize = BufferSize;

      gBS->FreePool (ConfigResp);
      return EFI_BUFFER_TOO_SMALL;
    }

    *ResultsDataSize = BufferSize;
    EfiCopyMem (ResultsData, StrPtr, BufferSize);

    gBS->FreePool (ConfigResp);
  } else {
    //
    // Prepare <ConfigResp>
    //
    BufferSize = (EfiStrLen (ResultsData) + EfiStrLen (Storage->ConfigHdr) + 2) * sizeof (CHAR16);
    ConfigResp = EfiLibAllocateZeroPool (BufferSize);
    ASSERT (ConfigResp != NULL);

    EfiStrCpy (ConfigResp, Storage->ConfigHdr);
    EfiStrCat (ConfigResp, L"&");
    EfiStrCat (ConfigResp, ResultsData);

    //
    // Update Browser uncommited data
    //
    Status = ConfigRespToStorage (Storage, ConfigResp);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
InitializeSetup (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
/*++

Routine Description:
  Initialize Setup

Arguments:
  (Standard EFI Image entry - EFI_IMAGE_ENTRY_POINT)

Returns:
  EFI_SUCCESS - Setup loaded.
  other       - Setup Error

--*/
{
  EFI_STATUS                  Status;
  EFI_HANDLE                  HiiDriverHandle;
  EFI_HII_PACKAGE_LIST_HEADER *PackageList;

  EfiInitializeDriverLib (ImageHandle, SystemTable);

  //
  // Locate required Hii relative protocols
  //
  Status = gBS->LocateProtocol (
                  &gEfiHiiDatabaseProtocolGuid,
                  NULL,
                  &mHiiDatabase
                  );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->LocateProtocol (
                  &gEfiHiiStringProtocolGuid,
                  NULL,
                  &mHiiString
                  );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->LocateProtocol (
                  &gEfiHiiConfigRoutingProtocolGuid,
                  NULL,
                  &mHiiConfigRouting
                  );
  ASSERT_EFI_ERROR (Status);

  //
  // Publish our HII data
  //
  Status = CreateHiiDriverHandle (&HiiDriverHandle);
  ASSERT_EFI_ERROR (Status);

  PackageList = PreparePackageList (1, &gSetupBrowserGuid, SetupBrowserStrings);
  ASSERT (PackageList != NULL);
  Status = mHiiDatabase->NewPackageList (
                           mHiiDatabase,
                           PackageList,
                           HiiDriverHandle,
                           &gHiiHandle
                           );
  ASSERT_EFI_ERROR (Status);

  //
  // Initialize Driver private data
  //
  gBannerData = EfiLibAllocateZeroPool (sizeof (BANNER_DATA));
  ASSERT (gBannerData != NULL);

  //
  // Install FormBrowser2 protocol
  //
  mPrivateData.Handle = NULL;
  Status = gBS->InstallProtocolInterface (
                  &mPrivateData.Handle,
                  &gEfiFormBrowser2ProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mPrivateData.FormBrowser2
                  );
  ASSERT_EFI_ERROR (Status);

  //
  // Install Print protocol
  //
  Status = gBS->InstallProtocolInterface (
                  &mPrivateData.Handle,
                  &gEfiPrintProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mPrivateData.Print
                  );

  return Status;
}

EFI_STRING_ID
NewString (
  IN  CHAR16                   *String,
  IN  EFI_HII_HANDLE           HiiHandle
  )
/*++

Routine Description:
  Create a new string in HII Package List.

Arguments:
  String      - The String to be added
  HiiHandle   - The package list in the HII database to insert the specified string.

Returns:
  The output string.

--*/
{
  EFI_STATUS     Status;
  EFI_STRING_ID  StringId;

  StringId = 0;
  Status = IfrLibNewString (HiiHandle, &StringId, String);

  return StringId;
}

EFI_STATUS
DeleteString (
  IN  EFI_STRING_ID            StringId,
  IN  EFI_HII_HANDLE           HiiHandle
  )
/*++

Routine Description:
  Delete a string from HII Package List.

Arguments:
  StringId    -  Id of the string in HII database.
  HiiHandle   -  The HII package list handle.

Returns:
  EFI_SUCCESS -  The string was deleted successfully.

--*/
{
  CHAR16  NullChar;

  NullChar = CHAR_NULL;
  return IfrLibSetString (HiiHandle, StringId, &NullChar);
}

CHAR16 *
GetToken (
  IN  EFI_STRING_ID                Token,
  IN  EFI_HII_HANDLE               HiiHandle
  )
/*++

Routine Description:
  Get the string based on the StringId and HII Package List Handle.

Arguments:

  Token       -  The String's ID.
  HiiHandle   -  The package list in the HII database to search for the specified string.

Returns:
  The output string.

--*/
{
  EFI_STATUS  Status;
  CHAR16      *String;
  UINTN       BufferLength;

  //
  // Set default string size assumption at no more than 256 bytes
  //
  BufferLength = 0x100;
  String = EfiLibAllocateZeroPool (BufferLength);
  ASSERT (String != NULL);

  Status = IfrLibGetString (HiiHandle, Token, String, &BufferLength);

  if (Status == EFI_BUFFER_TOO_SMALL) {
    gBS->FreePool (String);
    String = EfiLibAllocateZeroPool (BufferLength);
    ASSERT (String != NULL);

    Status = IfrLibGetString (HiiHandle, Token, String, &BufferLength);
  }
  ASSERT_EFI_ERROR (Status);

  return String;
}

VOID
NewStringCpy (
  IN OUT CHAR16       **Dest,
  IN CHAR16           *Src
  )
/*++

Routine Description:
  Allocate new memory and then copy the Unicode string Source to Destination.

Arguments:
  Dest     - Location to copy string
  Src      - String to copy

Returns:
  NONE

--*/
{
  EfiLibSafeFreePool (*Dest);
  *Dest = EfiLibAllocateCopyPool (EfiStrSize (Src), Src);
  ASSERT (*Dest != NULL);
}

VOID
NewStringCat (
  IN OUT CHAR16       **Dest,
  IN CHAR16           *Src
  )
/*++

Routine Description:
  Allocate new memory and concatinate Source on the end of Destination.

Arguments:
  Dest     - String to added to the end of.
  Src      - String to concatinate.

Returns:
  NONE

--*/
{
  CHAR16  *NewString;

  if (*Dest == NULL) {
    NewStringCpy (Dest, Src);
    return;
  }

  NewString = EfiLibAllocateZeroPool (EfiStrSize (*Dest) + EfiStrSize (Src) - 1);
  ASSERT (NewString != NULL);

  EfiStrCpy (NewString, *Dest);
  EfiStrCat (NewString, Src);

  gBS->FreePool (*Dest);
  *Dest = NewString;
}

VOID
SynchronizeStorage (
  IN FORMSET_STORAGE         *Storage
  )
/*++

Routine Description:
  Synchronize Storage's Edit copy to Shadow copy.

Arguments:
  Storage     -     The Storage to be synchronized.

Returns:
  NONE

--*/
{
  EFI_LIST_ENTRY          *Link;
  NAME_VALUE_NODE         *Node;

  switch (Storage->Type) {
  case EFI_HII_VARSTORE_BUFFER:
    EfiCopyMem (Storage->Buffer, Storage->EditBuffer, Storage->Size);
    break;

  case EFI_HII_VARSTORE_NAME_VALUE:
    Link = GetFirstNode (&Storage->NameValueListHead);
    while (!IsNull (&Storage->NameValueListHead, Link)) {
      Node = NAME_VALUE_NODE_FROM_LINK (Link);

      NewStringCpy (&Node->Value, Node->EditValue);

      Link = GetNextNode (&Storage->NameValueListHead, Link);
    }
    break;

  case EFI_HII_VARSTORE_EFI_VARIABLE:
  default:
    break;
  }
}

EFI_STATUS
GetValueByName (
  IN FORMSET_STORAGE         *Storage,
  IN CHAR16                  *Name,
  IN OUT CHAR16              **Value
  )
/*++

Routine Description:
  Get Value for given Name from a NameValue Storage.

Arguments:
  Storage  -     The NameValue Storage.
  Name     -     The Name.
  Value    -     The retured Value.

Returns:
  EFI_SUCCESS   - Value found for given Name.
  EFI_NOT_FOUND - No such Name found in NameValue storage.

--*/
{
  EFI_LIST_ENTRY          *Link;
  NAME_VALUE_NODE         *Node;

  *Value = NULL;

  Link = GetFirstNode (&Storage->NameValueListHead);
  while (!IsNull (&Storage->NameValueListHead, Link)) {
    Node = NAME_VALUE_NODE_FROM_LINK (Link);

    if (EfiStrCmp (Name, Node->Name) == 0) {
      NewStringCpy (Value, Node->EditValue);
      return EFI_SUCCESS;
    }

    Link = GetNextNode (&Storage->NameValueListHead, Link);
  }

  return EFI_NOT_FOUND;
}

EFI_STATUS
SetValueByName (
  IN FORMSET_STORAGE         *Storage,
  IN CHAR16                  *Name,
  IN CHAR16                  *Value
  )
/*++

Routine Description:
  Set Value of given Name in a NameValue Storage.

Arguments:
  Storage  -     The NameValue Storage.
  Name     -     The Name.
  Value    -     The Value to set.

Returns:
  EFI_SUCCESS   - Value found for given Name.
  EFI_NOT_FOUND - No such Name found in NameValue storage.

--*/
{
  EFI_LIST_ENTRY          *Link;
  NAME_VALUE_NODE         *Node;

  Link = GetFirstNode (&Storage->NameValueListHead);
  while (!IsNull (&Storage->NameValueListHead, Link)) {
    Node = NAME_VALUE_NODE_FROM_LINK (Link);

    if (EfiStrCmp (Name, Node->Name) == 0) {
      EfiLibSafeFreePool (Node->EditValue);
      Node->EditValue = EfiLibAllocateCopyPool (EfiStrSize (Value), Value);
      ASSERT (Node->EditValue != NULL);
      return EFI_SUCCESS;
    }

    Link = GetNextNode (&Storage->NameValueListHead, Link);
  }

  return EFI_NOT_FOUND;
}

EFI_STATUS
StorageToConfigResp (
  IN FORMSET_STORAGE         *Storage,
  IN CHAR16                  **ConfigResp
  )
/*++

Routine Description:
  Convert setting of Buffer Storage or NameValue Storage to <ConfigResp>.

Arguments:
  Storage     -     The Storage to be conveted.
  ConfigResp  -     The returned <ConfigResp>.

Returns:
  EFI_SUCCESS           - Convert success.
  EFI_INVALID_PARAMETER - Incorrect storage type.

--*/
{
  EFI_STATUS  Status;
  EFI_STRING  Progress;
  EFI_LIST_ENTRY          *Link;
  NAME_VALUE_NODE         *Node;

  Status = EFI_SUCCESS;

  switch (Storage->Type) {
  case EFI_HII_VARSTORE_BUFFER:
    Status = mHiiConfigRouting->BlockToConfig (
                                  mHiiConfigRouting,
                                  Storage->ConfigRequest,
                                  Storage->EditBuffer,
                                  Storage->Size,
                                  ConfigResp,
                                  &Progress
                                  );
    break;

  case EFI_HII_VARSTORE_NAME_VALUE:
    *ConfigResp = NULL;
    NewStringCat (ConfigResp, Storage->ConfigHdr);

    Link = GetFirstNode (&Storage->NameValueListHead);
    while (!IsNull (&Storage->NameValueListHead, Link)) {
      Node = NAME_VALUE_NODE_FROM_LINK (Link);

      NewStringCat (ConfigResp, L"&");
      NewStringCat (ConfigResp, Node->Name);
      NewStringCat (ConfigResp, L"=");
      NewStringCat (ConfigResp, Node->EditValue);

      Link = GetNextNode (&Storage->NameValueListHead, Link);
    }
    break;

  case EFI_HII_VARSTORE_EFI_VARIABLE:
  default:
    Status = EFI_INVALID_PARAMETER;
    break;
  }

  return Status;
}

EFI_STATUS
ConfigRespToStorage (
  IN FORMSET_STORAGE         *Storage,
  IN CHAR16                  *ConfigResp
  )
/*++

Routine Description:
  Convert <ConfigResp> to settings in Buffer Storage or NameValue Storage.

Arguments:
  Storage     -     The Storage to receive the settings.
  ConfigResp  -     The <ConfigResp> to be converted.

Returns:
  EFI_SUCCESS           - Convert success.
  EFI_INVALID_PARAMETER - Incorrect storage type.

--*/
{
  EFI_STATUS  Status;
  EFI_STRING  Progress;
  UINTN       BufferSize;
  CHAR16      *StrPtr;
  CHAR16      *Name;
  CHAR16      *Value;

  Status = EFI_SUCCESS;

  switch (Storage->Type) {
  case EFI_HII_VARSTORE_BUFFER:
    BufferSize = Storage->Size;
    Status = mHiiConfigRouting->ConfigToBlock (
                                  mHiiConfigRouting,
                                  ConfigResp,
                                  Storage->EditBuffer,
                                  &BufferSize,
                                  &Progress
                                  );
    break;

  case EFI_HII_VARSTORE_NAME_VALUE:
    StrPtr = EfiStrStr (ConfigResp, L"PATH");
    if (StrPtr == NULL) {
      break;
    }
    StrPtr = EfiStrStr (StrPtr, L"&");
    while (StrPtr != NULL) {
      //
      // Skip '&'
      //
      StrPtr = StrPtr + 1;
      Name = StrPtr;
      StrPtr = EfiStrStr (StrPtr, L"=");
      if (StrPtr == NULL) {
        break;
      }
      *StrPtr = 0;

      //
      // Skip '='
      //
      StrPtr = StrPtr + 1;
      Value = StrPtr;
      StrPtr = EfiStrStr (StrPtr, L"&");
      if (StrPtr != NULL) {
        *StrPtr = 0;
      }
      SetValueByName (Storage, Name, Value);
    }
    break;

  case EFI_HII_VARSTORE_EFI_VARIABLE:
  default:
    Status = EFI_INVALID_PARAMETER;
    break;
  }

  return Status;
}

EFI_STATUS
GetQuestionValue (
  IN FORM_BROWSER_FORMSET             *FormSet,
  IN FORM_BROWSER_FORM                *Form,
  IN OUT FORM_BROWSER_STATEMENT       *Question,
  IN BOOLEAN                          Cached
  )
/*++

Routine Description:
  Get Question's current Value.

Arguments:
  FormSet    -  FormSet data structure.
  Form       -  Form data structure.
  Question   -  Question to be initialized.
  Cached     -  TRUE:  get from Edit copy
                FALSE: get from original Storage

Returns:
  EFI_SUCCESS - The function completed successfully.

--*/
{
  EFI_STATUS          Status;
  BOOLEAN             Enabled;
  BOOLEAN             Pending;
  UINT8               *Dst;
  UINTN               StorageWidth;
  EFI_TIME            EfiTime;
  FORMSET_STORAGE     *Storage;
  EFI_IFR_TYPE_VALUE  *QuestionValue;
  CHAR16              *ConfigRequest;
  CHAR16              *Progress;
  CHAR16              *Result;
  CHAR16              *Value;
  CHAR16              *StringPtr;
  UINTN               Length;
  BOOLEAN             IsBufferStorage;
  BOOLEAN             IsString;

  Status = EFI_SUCCESS;

  //
  // Statement don't have storage, skip them
  //
  if (Question->QuestionId == 0) {
    return Status;
  }

  //
  // Question value is provided by an Expression, evaluate it
  //
  if (Question->ValueExpression != NULL) {
    Status = EvaluateExpression (FormSet, Form, Question->ValueExpression);
    if (!EFI_ERROR (Status)) {
      EfiCopyMem (&Question->HiiValue, &Question->ValueExpression->Result, sizeof (EFI_HII_VALUE));
    }
    return Status;
  }

  //
  // Question value is provided by RTC
  //
  Storage = Question->Storage;
  QuestionValue = &Question->HiiValue.Value;
  if (Storage == NULL) {
    //
    // It's a Question without storage, or RTC date/time
    //
    if (Question->Operand == EFI_IFR_DATE_OP || Question->Operand == EFI_IFR_TIME_OP) {
      //
      // Date and time define the same Flags bit
      //
      switch (Question->Flags & EFI_QF_DATE_STORAGE) {
      case QF_DATE_STORAGE_TIME:
        Status = gRT->GetTime (&EfiTime, NULL);
        break;

      case QF_DATE_STORAGE_WAKEUP:
        Status = gRT->GetWakeupTime (&Enabled, &Pending, &EfiTime);
        break;

      case QF_DATE_STORAGE_NORMAL:
      default:
        //
        // For date/time without storage
        //
        return EFI_SUCCESS;
      }

      if (EFI_ERROR (Status)) {
        return Status;
      }

      if (Question->Operand == EFI_IFR_DATE_OP) {
        QuestionValue->date.Year  = EfiTime.Year;
        QuestionValue->date.Month = EfiTime.Month;
        QuestionValue->date.Day   = EfiTime.Day;
      } else {
        QuestionValue->time.Hour   = EfiTime.Hour;
        QuestionValue->time.Minute = EfiTime.Minute;
        QuestionValue->time.Second = EfiTime.Second;
      }
    }

    return EFI_SUCCESS;
  }

  //
  // Question value is provided by EFI variable
  //
  StorageWidth = Question->StorageWidth;
  if (Storage->Type == EFI_HII_VARSTORE_EFI_VARIABLE) {
    if (Question->BufferValue != NULL) {
      Dst = Question->BufferValue;
    } else {
      Dst = (UINT8 *) QuestionValue;
    }

    Status = gRT->GetVariable (
                     Question->VariableName,
                     &Storage->Guid,
                     NULL,
                     &StorageWidth,
                     Dst
                     );
    //
    // Always return success, even this EFI variable doesn't exist
    //
    return EFI_SUCCESS;
  }

  //
  // Question Value is provided by Buffer Storage or NameValue Storage
  //
  if (Question->BufferValue != NULL) {
    //
    // This Question is password or orderedlist
    //
    Dst = Question->BufferValue;
  } else {
    //
    // Other type of Questions
    //
    Dst = (UINT8 *) &Question->HiiValue.Value;
  }

  IsBufferStorage = (Storage->Type == EFI_HII_VARSTORE_BUFFER) ? TRUE : FALSE;
  IsString = (Question->HiiValue.Type == EFI_IFR_TYPE_STRING) ?  TRUE : FALSE;
  if (Cached) {
    if (IsBufferStorage) {
      //
      // Copy from storage Edit buffer
      //
      EfiCopyMem (Dst, Storage->EditBuffer + Question->VarStoreInfo.VarOffset, StorageWidth);
    } else {
      Status = GetValueByName (Storage, Question->VariableName, &Value);
      if (EFI_ERROR (Status)) {
        return Status;
      }

      if (IsString) {
        //
        // Convert Config String to Unicode String, e.g "0041004200430044" => "ABCD"
        //
        Length = StorageWidth + sizeof (CHAR16);
        Status = ConfigStringToUnicode ((CHAR16 *) Dst, &Length, Value);
      } else {
        Status = HexStringToBuf (Dst, &StorageWidth, Value, NULL);
      }

      gBS->FreePool (Value);
    }
  } else {
    //
    // Request current settings from Configuration Driver
    //
    if (FormSet->ConfigAccess == NULL) {
      return EFI_NOT_FOUND;
    }

    //
    // <ConfigRequest> ::= <ConfigHdr> + <BlockName> ||
    //                   <ConfigHdr> + "&" + <VariableName>
    //
    if (IsBufferStorage) {
      Length = EfiStrLen (Storage->ConfigHdr) + EfiStrLen (Question->BlockName);
    } else {
      Length = EfiStrLen (Storage->ConfigHdr) + EfiStrLen (Question->VariableName) + 1;
    }
    ConfigRequest = EfiLibAllocateZeroPool ((Length + 1) * sizeof (CHAR16));
    ASSERT (ConfigRequest != NULL);

    EfiStrCpy (ConfigRequest, Storage->ConfigHdr);
    if (IsBufferStorage) {
      EfiStrCat (ConfigRequest, Question->BlockName);
    } else {
      EfiStrCat (ConfigRequest, L"&");
      EfiStrCat (ConfigRequest, Question->VariableName);
    }

    Status = FormSet->ConfigAccess->ExtractConfig (
                                      FormSet->ConfigAccess,
                                      ConfigRequest,
                                      &Progress,
                                      &Result
                                      );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Skip <ConfigRequest>
    //
    Value = Result + Length;
    if (IsBufferStorage) {
      //
      // Skip "&VALUE"
      //
      Value = Value + 6;
    }
    if (*Value != '=') {
      gBS->FreePool (Result);
      return EFI_NOT_FOUND;
    }
    //
    // Skip '=', point to value
    //
    Value = Value + 1;

    //
    // Suppress <AltResp> if any
    //
    StringPtr = Value;
    while (*StringPtr != L'\0' && *StringPtr != L'&') {
      StringPtr++;
    }
    *StringPtr = L'\0';

    if (!IsBufferStorage && IsString) {
      //
      // Convert Config String to Unicode String, e.g "0041004200430044" => "ABCD"
      //
      Length = StorageWidth + sizeof (CHAR16);
      Status = ConfigStringToUnicode ((CHAR16 *) Dst, &Length, Value);
    } else {
      Status = HexStringToBuf (Dst, &StorageWidth, Value, NULL);
      if (EFI_ERROR (Status)) {
        gBS->FreePool (Result);
        return Status;
      }
    }

    //
    // Synchronize Edit Buffer
    //
    if (IsBufferStorage) {
      EfiCopyMem (Storage->EditBuffer + Question->VarStoreInfo.VarOffset, Dst, StorageWidth);
    } else {
      SetValueByName (Storage, Question->VariableName, Value);
    }
    gBS->FreePool (Result);
  }

  return Status;
}

EFI_STATUS
SetQuestionValue (
  IN FORM_BROWSER_FORMSET             *FormSet,
  IN FORM_BROWSER_FORM                *Form,
  IN OUT FORM_BROWSER_STATEMENT       *Question,
  IN BOOLEAN                          Cached
  )
/*++

Routine Description:
  Save Question Value to edit copy(cached) or Storage(uncached).

Arguments:
  FormSet    -  FormSet data structure.
  Form       -  Form data structure.
  Question   -  Pointer to the Question.
  Cached     -  TRUE:  set to Edit copy
                FALSE: set to original Storage

Returns:
  EFI_SUCCESS -  The function completed successfully.

--*/
{
  EFI_STATUS          Status;
  BOOLEAN             Enabled;
  BOOLEAN             Pending;
  UINT8               *Src;
  EFI_TIME            EfiTime;
  UINTN               BufferLen;
  UINTN               StorageWidth;
  FORMSET_STORAGE     *Storage;
  EFI_IFR_TYPE_VALUE  *QuestionValue;
  CHAR16              *ConfigResp;
  CHAR16              *Progress;
  CHAR16              *Value;
  UINTN               Length;
  BOOLEAN             IsBufferStorage;
  BOOLEAN             IsString;

  Status = EFI_SUCCESS;

  //
  // Statement don't have storage, skip them
  //
  if (Question->QuestionId == 0) {
    return Status;
  }

  //
  // If Question value is provided by an Expression, then it is read only
  //
  if (Question->ValueExpression != NULL) {
    return Status;
  }

  //
  // Question value is provided by RTC
  //
  Storage = Question->Storage;
  QuestionValue = &Question->HiiValue.Value;
  if (Storage == NULL) {
    //
    // It's a Question without storage, or RTC date/time
    //
    if (Question->Operand == EFI_IFR_DATE_OP || Question->Operand == EFI_IFR_TIME_OP) {
      //
      // Date and time define the same Flags bit
      //
      switch (Question->Flags & EFI_QF_DATE_STORAGE) {
      case QF_DATE_STORAGE_TIME:
        Status = gRT->GetTime (&EfiTime, NULL);
        break;

      case QF_DATE_STORAGE_WAKEUP:
        Status = gRT->GetWakeupTime (&Enabled, &Pending, &EfiTime);
        break;

      case QF_DATE_STORAGE_NORMAL:
      default:
        //
        // For date/time without storage
        //
        return EFI_SUCCESS;
      }

      if (EFI_ERROR (Status)) {
        return Status;
      }

      if (Question->Operand == EFI_IFR_DATE_OP) {
        EfiTime.Year  = QuestionValue->date.Year;
        EfiTime.Month = QuestionValue->date.Month;
        EfiTime.Day   = QuestionValue->date.Day;
      } else {
        EfiTime.Hour   = QuestionValue->time.Hour;
        EfiTime.Minute = QuestionValue->time.Minute;
        EfiTime.Second = QuestionValue->time.Second;
      }

      if ((Question->Flags & EFI_QF_DATE_STORAGE) == QF_DATE_STORAGE_TIME) {
        Status = gRT->SetTime (&EfiTime);
      } else {
        Status = gRT->SetWakeupTime (TRUE, &EfiTime);
      }
    }

    return Status;
  }

  //
  // Question value is provided by EFI variable
  //
  StorageWidth = Question->StorageWidth;
  if (Storage->Type == EFI_HII_VARSTORE_EFI_VARIABLE) {
    if (Question->BufferValue != NULL) {
      Src = Question->BufferValue;
    } else {
      Src = (UINT8 *) QuestionValue;
    }

    Status = gRT->SetVariable (
                     Question->VariableName,
                     &Storage->Guid,
                     Storage->Attributes,
                     StorageWidth,
                     Src
                     );
    return Status;
  }

  //
  // Question Value is provided by Buffer Storage or NameValue Storage
  //
  if (Question->BufferValue != NULL) {
    Src = Question->BufferValue;
  } else {
    Src = (UINT8 *) &Question->HiiValue.Value;
  }

  IsBufferStorage = (Storage->Type == EFI_HII_VARSTORE_BUFFER) ? TRUE : FALSE;
  IsString = (Question->HiiValue.Type == EFI_IFR_TYPE_STRING) ?  TRUE : FALSE;
  if (IsBufferStorage) {
    //
    // Copy to storage edit buffer
    //
    EfiCopyMem (Storage->EditBuffer + Question->VarStoreInfo.VarOffset, Src, StorageWidth);
  } else {
    if (IsString) {
      //
      // Convert Unicode String to Config String, e.g. "ABCD" => "0041004200430044"
      //
      Value = NULL;
      BufferLen = ((EfiStrLen ((CHAR16 *) Src) * 4) + 1) * sizeof (CHAR16);
      Value = EfiLibAllocateZeroPool (BufferLen);
      ASSERT (Value != NULL);
      Status = UnicodeToConfigString (Value, &BufferLen, (CHAR16 *) Src);
      ASSERT_EFI_ERROR (Status);
    } else {
      BufferLen = StorageWidth * 2 + 1;
      Value = EfiLibAllocateZeroPool (BufferLen * sizeof (CHAR16));
      ASSERT (Value != NULL);
      BufToHexString (Value, &BufferLen, Src, StorageWidth);
      ToLower (Value);
    }

    Status = SetValueByName (Storage, Question->VariableName, Value);
    gBS->FreePool (Value);
  }

  if (!Cached) {
    //
    // <ConfigResp> ::= <ConfigHdr> + <BlockName> + "&VALUE=" + "<HexCh>StorageWidth * 2" ||
    //                <ConfigHdr> + "&" + <VariableName> + "=" + "<string>"
    //
    if (IsBufferStorage) {
      Length = EfiStrLen (Question->BlockName) + 7;
    } else {
      Length = EfiStrLen (Question->VariableName) + 2;
    }
    if (!IsBufferStorage && IsString) {
      Length += (EfiStrLen ((CHAR16 *) Src) * 4);
    } else {
      Length += (StorageWidth * 2);
    }
    ConfigResp = EfiLibAllocateZeroPool ((EfiStrLen (Storage->ConfigHdr) + Length + 1) * sizeof (CHAR16));
    ASSERT (ConfigResp != NULL);

    EfiStrCpy (ConfigResp, Storage->ConfigHdr);
    if (IsBufferStorage) {
      EfiStrCat (ConfigResp, Question->BlockName);
      EfiStrCat (ConfigResp, L"&VALUE=");
    } else {
      EfiStrCat (ConfigResp, L"&");
      EfiStrCat (ConfigResp, Question->VariableName);
      EfiStrCat (ConfigResp, L"=");
    }

    Value = ConfigResp + EfiStrLen (ConfigResp);
    if (!IsBufferStorage && IsString) {
      //
      // Convert Unicode String to Config String, e.g. "ABCD" => "0041004200430044"
      //
      BufferLen = ((EfiStrLen ((CHAR16 *) Src) * 4) + 1) * sizeof (CHAR16);
      Status = UnicodeToConfigString (Value, &BufferLen, (CHAR16 *) Src);
      ASSERT_EFI_ERROR (Status);
    } else {
      BufferLen = StorageWidth * 2 + 1;
      BufToHexString (Value, &BufferLen, Src, StorageWidth);
      ToLower (Value);
    }

    //
    // Submit Question Value to Configuration Driver
    //
    if (FormSet->ConfigAccess != NULL) {
      Status = FormSet->ConfigAccess->RouteConfig (
                                        FormSet->ConfigAccess,
                                        ConfigResp,
                                        &Progress
                                        );
      if (EFI_ERROR (Status)) {
        gBS->FreePool (ConfigResp);
        return Status;
      }
    }
    gBS->FreePool (ConfigResp);

    //
    // Synchronize shadow Buffer
    //
    SynchronizeStorage (Storage);
  }

  return Status;
}

EFI_STATUS
ValidateQuestion (
  IN  FORM_BROWSER_FORMSET            *FormSet,
  IN  FORM_BROWSER_FORM               *Form,
  IN  FORM_BROWSER_STATEMENT          *Question,
  IN  UINTN                           Type
  )
/*++

Routine Description:
  Perform inconsistent check for a Form.

Arguments:
  FormSet      -  FormSet data structure.
  Form         -  Form data structure.
  Question     -  The Question to be validated.
  Type         -  Validation type: InConsistent or NoSubmit

Returns:
  EFI_SUCCESS  -  Form validation pass.
  other        -  Form validation failed.

--*/
{
  EFI_STATUS              Status;
  EFI_LIST_ENTRY          *Link;
  EFI_LIST_ENTRY          *ListHead;
  EFI_STRING              PopUp;
  EFI_INPUT_KEY           Key;
  FORM_EXPRESSION         *Expression;

  if (Type == EFI_HII_EXPRESSION_INCONSISTENT_IF) {
    ListHead = &Question->InconsistentListHead;
  } else if (Type == EFI_HII_EXPRESSION_NO_SUBMIT_IF) {
    ListHead = &Question->NoSubmitListHead;
  } else {
    return EFI_UNSUPPORTED;
  }

  Link = GetFirstNode (ListHead);
  while (!IsNull (ListHead, Link)) {
    Expression = FORM_EXPRESSION_FROM_LINK (Link);

    //
    // Evaluate the expression
    //
    Status = EvaluateExpression (FormSet, Form, Expression);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Expression->Result.Value.b) {
      //
      // Condition meet, show up error message
      //
      if (Expression->Error != 0) {
        PopUp = GetToken (Expression->Error, FormSet->HiiHandle);
        do {
          CreateDialog (4, TRUE, 0, NULL, &Key, gEmptyString, PopUp, gPressEnter, gEmptyString);
        } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);
        gBS->FreePool (PopUp);
      }

      return EFI_NOT_READY;
    }

    Link = GetNextNode (ListHead, Link);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
NoSubmitCheck (
  IN  FORM_BROWSER_FORMSET            *FormSet,
  IN  FORM_BROWSER_FORM               *Form
  )
/*++

Routine Description:
  Perform NoSubmit check for a Form.

Arguments:
  FormSet      -  FormSet data structure.
  Form         -  Form data structure.

Returns:
  EFI_SUCCESS  -  Form validation pass.
  other        -  Form validation failed.

--*/
{
  EFI_STATUS              Status;
  EFI_LIST_ENTRY          *Link;
  FORM_BROWSER_STATEMENT  *Question;

  Link = GetFirstNode (&Form->StatementListHead);
  while (!IsNull (&Form->StatementListHead, Link)) {
    Question = FORM_BROWSER_STATEMENT_FROM_LINK (Link);

    Status = ValidateQuestion (FormSet, Form, Question, EFI_HII_EXPRESSION_NO_SUBMIT_IF);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Link = GetNextNode (&Form->StatementListHead, Link);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
SubmitForm (
  IN FORM_BROWSER_FORMSET             *FormSet,
  IN FORM_BROWSER_FORM                *Form
  )
/*++

Routine Description:
  Submit a Form.

Arguments:
  FormSet     -  FormSet data structure.
  Form        -  Form data structure.

Returns:
  EFI_SUCCESS -  The function completed successfully.

--*/
{
  EFI_STATUS              Status;
  EFI_LIST_ENTRY          *Link;
  EFI_STRING              ConfigResp;
  EFI_STRING              Progress;
  FORMSET_STORAGE         *Storage;

  //
  // Validate the Form by NoSubmit check
  //
  Status = NoSubmitCheck (FormSet, Form);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Submit Buffer storage or Name/Value storage
  //
  Link = GetFirstNode (&FormSet->StorageListHead);
  while (!IsNull (&FormSet->StorageListHead, Link)) {
    Storage = FORMSET_STORAGE_FROM_LINK (Link);
    Link = GetNextNode (&FormSet->StorageListHead, Link);

    if (Storage->Type == EFI_HII_VARSTORE_EFI_VARIABLE) {
      continue;
    }

    //
    // Skip if there is no RequestElement
    //
    if (Storage->ElementCount == 0) {
      continue;
    }

    //
    // Prepare <ConfigResp>
    //
    Status = StorageToConfigResp (Storage, &ConfigResp);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Send <ConfigResp> to Configuration Driver
    //
    if (FormSet->ConfigAccess != NULL) {
      Status = FormSet->ConfigAccess->RouteConfig (
                                        FormSet->ConfigAccess,
                                        ConfigResp,
                                        &Progress
                                        );
      if (EFI_ERROR (Status)) {
        gBS->FreePool (ConfigResp);
        return Status;
      }
    }
    gBS->FreePool (ConfigResp);

    //
    // Config success, update storage shadow Buffer
    //
    SynchronizeStorage (Storage);
  }

  gNvUpdateRequired = FALSE;

  return EFI_SUCCESS;
}

EFI_STATUS
GetQuestionDefault (
  IN FORM_BROWSER_FORMSET             *FormSet,
  IN FORM_BROWSER_FORM                *Form,
  IN FORM_BROWSER_STATEMENT           *Question,
  IN UINT16                           DefaultId
  )
/*++

Routine Description:
  Reset Question to its default value.

Arguments:
  FormSet     -  FormSet data structure.
  DefaultId   -  The Class of the default.

Returns:
  EFI_SUCCESS  - Question is reset to default value.

--*/
{
  EFI_STATUS              Status;
  EFI_LIST_ENTRY          *Link;
  QUESTION_DEFAULT        *Default;
  QUESTION_OPTION         *Option;
  EFI_HII_VALUE           *HiiValue;
  UINT8                   Index;

  Status = EFI_SUCCESS;

  //
  // Statement don't have storage, skip them
  //
  if (Question->QuestionId == 0) {
    return Status;
  }

  //
  // There are three ways to specify default value for a Question:
  //  1, use nested EFI_IFR_DEFAULT (highest priority)
  //  2, set flags of EFI_ONE_OF_OPTION (provide Standard and Manufacturing default)
  //  3, set flags of EFI_IFR_CHECKBOX (provide Standard and Manufacturing default) (lowest priority)
  //
  HiiValue = &Question->HiiValue;

  //
  // EFI_IFR_DEFAULT has highest priority
  //
  if (!IsListEmpty (&Question->DefaultListHead)) {
    Link = GetFirstNode (&Question->DefaultListHead);
    while (!IsNull (&Question->DefaultListHead, Link)) {
      Default = QUESTION_DEFAULT_FROM_LINK (Link);

      if (Default->DefaultId == DefaultId) {
        if (Default->ValueExpression != NULL) {
          //
          // Default is provided by an Expression, evaluate it
          //
          Status = EvaluateExpression (FormSet, Form, Default->ValueExpression);
          if (EFI_ERROR (Status)) {
            return Status;
          }

          EfiCopyMem (HiiValue, &Default->ValueExpression->Result, sizeof (EFI_HII_VALUE));
        } else {
          //
          // Default value is embedded in EFI_IFR_DEFAULT
          //
          EfiCopyMem (HiiValue, &Default->Value, sizeof (EFI_HII_VALUE));
        }

        return EFI_SUCCESS;
      }

      Link = GetNextNode (&Question->DefaultListHead, Link);
    }
  }

  //
  // EFI_ONE_OF_OPTION
  //
  if ((Question->Operand == EFI_IFR_ONE_OF_OP) && !IsListEmpty (&Question->OptionListHead)) {
    if (DefaultId <= EFI_HII_DEFAULT_CLASS_MANUFACTURING)  {
      //
      // OneOfOption could only provide Standard and Manufacturing default
      //
      Link = GetFirstNode (&Question->OptionListHead);
      while (!IsNull (&Question->OptionListHead, Link)) {
        Option = QUESTION_OPTION_FROM_LINK (Link);

        if (((DefaultId == EFI_HII_DEFAULT_CLASS_STANDARD) && (Option->Flags & EFI_IFR_OPTION_DEFAULT)) ||
            ((DefaultId == EFI_HII_DEFAULT_CLASS_MANUFACTURING) && (Option->Flags & EFI_IFR_OPTION_DEFAULT_MFG))
           ) {
          EfiCopyMem (HiiValue, &Option->Value, sizeof (EFI_HII_VALUE));

          return EFI_SUCCESS;
        }

        Link = GetNextNode (&Question->OptionListHead, Link);
      }
    }
  }

  //
  // EFI_IFR_CHECKBOX - lowest priority
  //
  if (Question->Operand == EFI_IFR_CHECKBOX_OP) {
    if (DefaultId <= EFI_HII_DEFAULT_CLASS_MANUFACTURING)  {
      //
      // Checkbox could only provide Standard and Manufacturing default
      //
      if (((DefaultId == EFI_HII_DEFAULT_CLASS_STANDARD) && (Question->Flags & EFI_IFR_CHECKBOX_DEFAULT)) ||
          ((DefaultId == EFI_HII_DEFAULT_CLASS_MANUFACTURING) && (Question->Flags & EFI_IFR_CHECKBOX_DEFAULT_MFG))
         ) {
        HiiValue->Value.b = TRUE;
      } else {
        HiiValue->Value.b = FALSE;
      }

      return EFI_SUCCESS;
    }
  }

  //
  // For Questions without default
  //
  switch (Question->Operand) {
  case EFI_IFR_ONE_OF_OP:
    //
    // Take first oneof option as oneof's default value
    //
    if (ValueToOption (Question, HiiValue) == NULL) {
      Link = GetFirstNode (&Question->OptionListHead);
      if (!IsNull (&Question->OptionListHead, Link)) {
        Option = QUESTION_OPTION_FROM_LINK (Link);
        EfiCopyMem (HiiValue, &Option->Value, sizeof (EFI_HII_VALUE));
      }
    }
    break;

  case EFI_IFR_ORDERED_LIST_OP:
    //
    // Take option sequence in IFR as ordered list's default value
    //
    Index = 0;
    Link = GetFirstNode (&Question->OptionListHead);
    while (!IsNull (&Question->OptionListHead, Link)) {
      Option = QUESTION_OPTION_FROM_LINK (Link);

      SetArrayData (Question->BufferValue, Question->ValueType, Index, Option->Value.Value.u64);

      Index++;
      if (Index >= Question->MaxContainers) {
        break;
      }

      Link = GetNextNode (&Question->OptionListHead, Link);
    }
    break;

  default:
    Status = EFI_NOT_FOUND;
    break;
  }

  return Status;
}

EFI_STATUS
ExtractFormDefault (
  IN FORM_BROWSER_FORMSET             *FormSet,
  IN FORM_BROWSER_FORM                *Form,
  IN UINT16                           DefaultId
  )
/*++

Routine Description:
  Reset Questions in a Form to their default value.

Arguments:
  FormSet     -  FormSet data structure.
  Form        -  The Form which to be reset.
  DefaultId   -  The Class of the default.

Returns:
  EFI_SUCCESS    -  The function completed successfully.

--*/
{
  EFI_STATUS              Status;
  EFI_LIST_ENTRY          *Link;
  FORM_BROWSER_STATEMENT  *Question;

  Link = GetFirstNode (&Form->StatementListHead);
  while (!IsNull (&Form->StatementListHead, Link)) {
    Question = FORM_BROWSER_STATEMENT_FROM_LINK (Link);
    Link = GetNextNode (&Form->StatementListHead, Link);

    //
    // If Question is disabled, don't reset it to default
    //
    if (Question->DisableExpression != NULL) {
      Status = EvaluateExpression (FormSet, Form, Question->DisableExpression);
      if (!EFI_ERROR (Status) && Question->DisableExpression->Result.Value.b) {
        continue;
      }
    }

    //
    // Reset Question to its default value
    //
    Status = GetQuestionDefault (FormSet, Form, Question, DefaultId);
    if (EFI_ERROR (Status)) {
      continue;
    }

    //
    // Synchronize Buffer storage's Edit buffer
    //
    if ((Question->Storage != NULL) &&
        (Question->Storage->Type != EFI_HII_VARSTORE_EFI_VARIABLE)) {
      SetQuestionValue (FormSet, Form, Question, TRUE);
    }
  }

  return EFI_SUCCESS;
}

EFI_STATUS
LoadFormConfig (
  IN FORM_BROWSER_FORMSET             *FormSet,
  IN FORM_BROWSER_FORM                *Form
  )
/*++

Routine Description:
  Initialize Question's Edit copy from Storage.

Arguments:
  FormSet     -  FormSet data structure.
  Form        -  Form data structure.

Returns:
  EFI_SUCCESS    -  The function completed successfully.

--*/
{
  EFI_STATUS              Status;
  EFI_LIST_ENTRY          *Link;
  FORM_BROWSER_STATEMENT  *Question;

  Link = GetFirstNode (&Form->StatementListHead);
  while (!IsNull (&Form->StatementListHead, Link)) {
    Question = FORM_BROWSER_STATEMENT_FROM_LINK (Link);

    //
    // Initialize local copy of Value for each Question
    //
    Status = GetQuestionValue (FormSet, Form, Question, TRUE);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Link = GetNextNode (&Form->StatementListHead, Link);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
LoadFormSetConfig (
  IN FORM_BROWSER_FORMSET             *FormSet
  )
/*++

Routine Description:
  Initialize Question's Edit copy from Storage for the whole Formset.

Arguments:
  FormSet     -  FormSet data structure.

Returns:
  EFI_SUCCESS    -  The function completed successfully.

--*/
{
  EFI_STATUS              Status;
  EFI_LIST_ENTRY          *Link;
  FORM_BROWSER_FORM       *Form;

  Link = GetFirstNode (&FormSet->FormListHead);
  while (!IsNull (&FormSet->FormListHead, Link)) {
    Form = FORM_BROWSER_FORM_FROM_LINK (Link);

    //
    // Initialize local copy of Value for each Form
    //
    Status = LoadFormConfig (FormSet, Form);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Link = GetNextNode (&FormSet->FormListHead, Link);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
LoadStorage (
  IN FORM_BROWSER_FORMSET    *FormSet,
  IN FORMSET_STORAGE         *Storage
  )
/*++

Routine Description:
  Fill storage's edit copy with settings requested from Configuration Driver.

Arguments:
  FormSet     -     FormSet data structure.
  Storage     -     Buffer Storage.

Returns:
  EFI_SUCCESS    -  The function completed successfully.

--*/
{
  EFI_STATUS  Status;
  EFI_STRING  Progress;
  EFI_STRING  Result;
  CHAR16      *StrPtr;

  if (Storage->Type == EFI_HII_VARSTORE_EFI_VARIABLE) {
    return EFI_SUCCESS;
  }

  if (FormSet->ConfigAccess == NULL) {
    return EFI_NOT_FOUND;
  }

  if (Storage->ElementCount == 0) {
    //
    // Skip if there is no RequestElement
    //
    return EFI_SUCCESS;
  }

  //
  // Request current settings from Configuration Driver
  //
  Status = FormSet->ConfigAccess->ExtractConfig (
                                    FormSet->ConfigAccess,
                                    Storage->ConfigRequest,
                                    &Progress,
                                    &Result
                                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Convert Result from <ConfigAltResp> to <ConfigResp>
  //
  StrPtr = EfiStrStr (Result, L"ALTCFG");
  if (StrPtr != NULL) {
    *StrPtr = L'\0';
  }

  Status = ConfigRespToStorage (Storage, Result);
  gBS->FreePool (Result);
  return Status;
}

EFI_STATUS
CopyStorage (
  IN OUT FORMSET_STORAGE     *Dst,
  IN FORMSET_STORAGE         *Src
  )
/*++

Routine Description:
  Copy uncommitted data from source Storage to destination Storage.

Arguments:
  Dst     -  Target Storage for uncommitted data.
  Src     -  Source Storage for uncommitted data.

Returns:
  EFI_SUCCESS           -  The function completed successfully.
  EFI_INVALID_PARAMETER -  Source and destination Storage is not the same type.

--*/
{
  EFI_LIST_ENTRY          *Link;
  NAME_VALUE_NODE         *Node;

  if ((Dst->Type != Src->Type) || (Dst->Size != Src->Size)) {
    return EFI_INVALID_PARAMETER;
  }

  switch (Src->Type) {
  case EFI_HII_VARSTORE_BUFFER:
    EfiCopyMem (Dst->EditBuffer, Src->EditBuffer, Src->Size);
    break;

  case EFI_HII_VARSTORE_NAME_VALUE:
    Link = GetFirstNode (&Src->NameValueListHead);
    while (!IsNull (&Src->NameValueListHead, Link)) {
      Node = NAME_VALUE_NODE_FROM_LINK (Link);

      SetValueByName (Dst, Node->Name, Node->EditValue);

      Link = GetNextNode (&Src->NameValueListHead, Link);
    }
    break;

  case EFI_HII_VARSTORE_EFI_VARIABLE:
  default:
    break;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
InitializeCurrentSetting (
  IN OUT FORM_BROWSER_FORMSET             *FormSet
  )
/*++

Routine Description:
  Get current setting of Questions.

Arguments:
  FormSet     -     FormSet data structure.

Returns:
  EFI_SUCCESS    -  The function completed successfully.

--*/
{
  EFI_STATUS              Status;
  EFI_LIST_ENTRY          *Link;
  EFI_LIST_ENTRY          *Link2;
  FORMSET_STORAGE         *Storage;
  FORMSET_STORAGE         *StorageSrc;
  FORMSET_STORAGE         *OldStorage;
  FORM_BROWSER_FORM       *Form;

  Status = EFI_SUCCESS;

  //
  // Extract default from IFR binary
  //
  Link = GetFirstNode (&FormSet->FormListHead);
  while (!IsNull (&FormSet->FormListHead, Link)) {
    Form = FORM_BROWSER_FORM_FROM_LINK (Link);

    Status = ExtractFormDefault (FormSet, Form, EFI_HII_DEFAULT_CLASS_STANDARD);

    Link = GetNextNode (&FormSet->FormListHead, Link);
  }

  //
  // Request current settings from Configuration Driver
  //
  Link = GetFirstNode (&FormSet->StorageListHead);
  while (!IsNull (&FormSet->StorageListHead, Link)) {
    Storage = FORMSET_STORAGE_FROM_LINK (Link);

    OldStorage = NULL;
    if (gOldFormSet != NULL) {
      //
      // Try to find the Storage in backup formset gOldFormSet
      //
      Link2 = GetFirstNode (&gOldFormSet->StorageListHead);
      while (!IsNull (&gOldFormSet->StorageListHead, Link2)) {
        StorageSrc = FORMSET_STORAGE_FROM_LINK (Link2);

        if (StorageSrc->VarStoreId == Storage->VarStoreId) {
          OldStorage = StorageSrc;
          break;
        }

        Link2 = GetNextNode (&gOldFormSet->StorageListHead, Link2);
      }
    }

    if (OldStorage == NULL) {
      //
      // Storage is not found in backup formset, request it from ConfigDriver
      //
      Status = LoadStorage (FormSet, Storage);
    } else {
      //
      // Storage found in backup formset, use it
      //
      Status = CopyStorage (Storage, OldStorage);
    }

    //
    // Now Edit Buffer is filled with default values(lower priority) and current
    // settings(higher priority), sychronize it to shadow Buffer
    //
    SynchronizeStorage (Storage);

    Link = GetNextNode (&FormSet->StorageListHead, Link);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
GetIfrBinaryData (
  IN  EFI_HII_HANDLE   Handle,
  IN OUT EFI_GUID      *FormSetGuid,
  OUT UINTN            *BinaryLength,
  OUT UINT8            **BinaryData
  )
/*++

Routine Description:
  Fetch the Ifr binary data of a FormSet.

Arguments:
  Handle         -  PackageList Handle
  FormSetGuid    -  GUID of a formset. If not specified (NULL or zero GUID),
                    take the first FormSet found in package list.
  BinaryLength   -  The length of the FormSet IFR binary.
  BinaryData     -  The buffer designed to receive the FormSet.

Returns:
  EFI_SUCCESS           -  Buffer filled with the requested FormSet. BufferLength was updated.
  EFI_INVALID_PARAMETER -  The handle is unknown.
  EFI_NOT_FOUND         -  A form or FormSet on the requested handle cannot be
                           found with the requested FormId.

--*/
{
  EFI_STATUS                   Status;
  EFI_HII_PACKAGE_LIST_HEADER  *HiiPackageList;
  UINTN                        BufferSize;
  UINT8                        *Package;
  UINT8                        *OpCodeData;
  UINT32                       Offset;
  UINT32                       Offset2;
  BOOLEAN                      ReturnDefault;
  UINT32                       PackageListLength;
  EFI_HII_PACKAGE_HEADER       PackageHeader;
  UINT8                        Index;
  UINT8                        NumberOfClassGuid;
  BOOLEAN                      IsSetupClassGuid;

  OpCodeData = NULL;
  Package = NULL;
  EfiZeroMem (&PackageHeader, sizeof (EFI_HII_PACKAGE_HEADER));;

  //
  // if FormSetGuid is NULL or zero GUID, return first FormSet in the package list
  //
  if (FormSetGuid == NULL || EfiCompareGuid (FormSetGuid, &gZeroGuid)) {
    ReturnDefault = TRUE;
  } else {
    ReturnDefault = FALSE;
  }

  //
  // Get HII PackageList
  //
  BufferSize = 0;
  HiiPackageList = NULL;
  Status = mHiiDatabase->ExportPackageLists (mHiiDatabase, Handle, &BufferSize, HiiPackageList);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    HiiPackageList = EfiLibAllocatePool (BufferSize);
    ASSERT (HiiPackageList != NULL);

    Status = mHiiDatabase->ExportPackageLists (mHiiDatabase, Handle, &BufferSize, HiiPackageList);
  }
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Get Form package from this HII package List
  //
  Offset = sizeof (EFI_HII_PACKAGE_LIST_HEADER);
  Offset2 = 0;
  EfiCopyMem (&PackageListLength, &HiiPackageList->PackageLength, sizeof (UINT32));

  while (Offset < PackageListLength) {
    Package = ((UINT8 *) HiiPackageList) + Offset;
    EfiCopyMem (&PackageHeader, Package, sizeof (EFI_HII_PACKAGE_HEADER));

    if (PackageHeader.Type == EFI_HII_PACKAGE_FORMS) {
      //
      // Search FormSet in this Form Package
      //
      Offset2 = sizeof (EFI_HII_PACKAGE_HEADER);
      while (Offset2 < PackageHeader.Length) {
        OpCodeData = Package + Offset2;

        if (((EFI_IFR_OP_HEADER *) OpCodeData)->OpCode == EFI_IFR_FORM_SET_OP) {
          //
          // Check whether return default FormSet
          //
          if (ReturnDefault) {
            if (((EFI_IFR_OP_HEADER *) OpCodeData)->Length <= ((UINTN) &((EFI_IFR_FORM_SET *) 0)->Flags)) {
              //
              // This is the old version of formset OpCode
              //
              break;
            }

            //
            // This is new version of formset OpCode, check whether ClassGuid
            //
            IsSetupClassGuid = FALSE;
            NumberOfClassGuid = ((EFI_IFR_FORM_SET *) OpCodeData)->Flags & 0x3;
            for (Index = 0; Index < NumberOfClassGuid; Index++) {
              if (EfiCompareMem (&((EFI_IFR_FORM_SET *) OpCodeData)->ClassGuid[Index], &gPlatformSetupClassGuid, sizeof (EFI_GUID)) == 0) {
                IsSetupClassGuid = TRUE;
                break;
              }
            }
            if (IsSetupClassGuid) {
              break;
            }
          }

          //
          // FormSet GUID is specified, check it
          //
          if (EfiCompareGuid (FormSetGuid, &((EFI_IFR_FORM_SET *) OpCodeData)->Guid)) {
            break;
          }
        }

        Offset2 += ((EFI_IFR_OP_HEADER *) OpCodeData)->Length;
      }

      if (Offset2 < PackageHeader.Length) {
        //
        // Target formset found
        //
        break;
      }
    }

    Offset += PackageHeader.Length;
  }

  if (Offset >= PackageListLength) {
    //
    // Form package not found in this Package List
    //
    gBS->FreePool (HiiPackageList);
    return EFI_NOT_FOUND;
  }

  if (ReturnDefault && FormSetGuid != NULL) {
    //
    // Return the default FormSet GUID
    //
    EfiCopyMem (FormSetGuid, &((EFI_IFR_FORM_SET *) OpCodeData)->Guid, sizeof (EFI_GUID));
  }

  //
  // To determine the length of a whole FormSet IFR binary, one have to parse all the Opcodes
  // in this FormSet; So, here just simply copy the data from start of a FormSet to the end
  // of the Form Package.
  //
  *BinaryLength = PackageHeader.Length - Offset2;
  *BinaryData = EfiLibAllocateCopyPool (*BinaryLength, OpCodeData);

  gBS->FreePool (HiiPackageList);

  if (*BinaryData == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
InitializeFormSet (
  IN  EFI_HII_HANDLE                   Handle,
  IN OUT EFI_GUID                      *FormSetGuid,
  OUT FORM_BROWSER_FORMSET             *FormSet
  )
/*++

Routine Description:
  Initialize the internal data structure of a FormSet.

Arguments:
  Handle      -     PackageList Handle
  FormSetGuid -     GUID of a formset. If not specified (NULL or zero GUID),
                    take the first FormSet found in package list.
  FormSet     -     FormSet data structure.

Returns:
  EFI_SUCCESS    -  The function completed successfully.
  EFI_NOT_FOUND  -  The specified FormSet could not be found.

--*/
{
  EFI_STATUS                Status;
  EFI_HANDLE                DriverHandle;
  UINT16                    Index;

  Status = GetIfrBinaryData (Handle, FormSetGuid, &FormSet->IfrBinaryLength, &FormSet->IfrBinaryData);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  FormSet->HiiHandle = Handle;
  EfiCopyMem (&FormSet->Guid, FormSetGuid, sizeof (EFI_GUID));

  //
  // Retrieve ConfigAccess Protocol associated with this HiiPackageList
  //
  Status = mHiiDatabase->GetPackageListHandle (mHiiDatabase, Handle, &DriverHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  FormSet->DriverHandle = DriverHandle;
  Status = gBS->HandleProtocol (
                  DriverHandle,
                  &gEfiHiiConfigAccessProtocolGuid,
                  &FormSet->ConfigAccess
                  );
  if (EFI_ERROR (Status)) {
    //
    // Configuration Driver don't attach ConfigAccess protocol to its HII package
    // list, then there will be no configuration action required
    //
    FormSet->ConfigAccess = NULL;
  }

  //
  // Parse the IFR binary OpCodes
  //
  FormSet->SubClass = 0xffff;
  Status = ParseOpCodes (FormSet);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gClassOfVfr = 0;
#ifdef TIANO_EXTENDED_CLASS_SUBCLASS_SUPPORT
  if (FormSet->SubClass != 0xffff) {
    //
    // Extended subclass opcode(EFI_IFR_EXTEND_OP_SUBCLASS) was extracted from IFR
    //
    if (FormSet->SubClass == EFI_FRONT_PAGE_SUBCLASS) {
      gClassOfVfr = FORMSET_CLASS_FRONT_PAGE;
    } else {
      gClassOfVfr = FORMSET_CLASS_PLATFORM_SETUP;
    }
  }
#endif

  if (IsClassGuidMatch (FormSet, &gFrontPageClassGuid)) {
    gClassOfVfr |= FORMSET_CLASS_FRONT_PAGE;
  }
  if (IsClassGuidMatch (FormSet, &gPlatformSetupClassGuid)) {
    gClassOfVfr |= FORMSET_CLASS_PLATFORM_SETUP;
  }

  if ((gClassOfVfr & FORMSET_CLASS_FRONT_PAGE) != 0) {
    gFrontPageHandle = FormSet->HiiHandle;
  }

  //
  // Match GUID to find out the function key setting. If match fail, use the default setting.
  //
  for (Index = 0; Index < sizeof (gFunctionKeySettingTable) / sizeof (FUNCTIION_KEY_SETTING); Index++) {
    if (EfiCompareGuid (&FormSet->Guid, &(gFunctionKeySettingTable[Index].FormSetGuid))) {
      //
      // Update the function key setting.
      //
      gFunctionKeySetting = gFunctionKeySettingTable[Index].KeySetting;
      //
      // Function key prompt can not be displayed if the function key has been disabled.
      //
      if ((gFunctionKeySetting & FUNCTION_NINE) != FUNCTION_NINE) {
        gFunctionNineString = GetToken (STRING_TOKEN (EMPTY_STRING), gHiiHandle);
      }

      if ((gFunctionKeySetting & FUNCTION_TEN) != FUNCTION_TEN) {
        gFunctionTenString = GetToken (STRING_TOKEN (EMPTY_STRING), gHiiHandle);
      }
    }
  }

  return Status;
}

VOID
SaveBrowserContext (
  VOID
  )
/*++

Routine Description:
  Save globals used by previous call to SendForm(). SendForm() may be called from 
  HiiConfigAccess.Callback(), this will cause SendForm() be reentried.
  So, save globals of previous call to SendForm() and restore them upon exit.

Arguments:
  None.

Returns:
  None.

--*/
{
  BROWSER_CONTEXT  *Context;

  gBrowserContextCount++;
  if (gBrowserContextCount == 1) {
    //
    // This is not reentry of SendForm(), no context to save
    //
    return;
  }

  Context = EfiLibAllocatePool (sizeof (BROWSER_CONTEXT));
  ASSERT (Context != NULL);

  Context->Signature = BROWSER_CONTEXT_SIGNATURE;

  //
  // Save FormBrowser context
  //
  Context->BannerData           = gBannerData;
  Context->ClassOfVfr           = gClassOfVfr;
  Context->FunctionKeySetting   = gFunctionKeySetting;
  Context->ResetRequired        = gResetRequired;
  Context->NvUpdateRequired     = gNvUpdateRequired;
  Context->Direction            = gDirection;
  Context->FunctionNineString   = gFunctionNineString;
  Context->FunctionTenString    = gFunctionTenString;
  Context->EnterString          = gEnterString;
  Context->EnterCommitString    = gEnterCommitString;
  Context->EnterEscapeString    = gEnterEscapeString;
  Context->EscapeString         = gEscapeString;
  Context->SaveFailed           = gSaveFailed;
  Context->MoveHighlight        = gMoveHighlight;
  Context->MakeSelection        = gMakeSelection;
  Context->DecNumericInput      = gDecNumericInput;
  Context->HexNumericInput      = gHexNumericInput;
  Context->ToggleCheckBox       = gToggleCheckBox;
  Context->PromptForData        = gPromptForData;
  Context->PromptForPassword    = gPromptForPassword;
  Context->PromptForNewPassword = gPromptForNewPassword;
  Context->ConfirmPassword      = gConfirmPassword;
  Context->ConfirmError         = gConfirmError;
  Context->PassowordInvalid     = gPassowordInvalid;
  Context->PressEnter           = gPressEnter;
  Context->EmptyString          = gEmptyString;
  Context->AreYouSure           = gAreYouSure;
  Context->YesResponse          = gYesResponse;
  Context->NoResponse           = gNoResponse;
  Context->MiniString           = gMiniString;
  Context->PlusString           = gPlusString;
  Context->MinusString          = gMinusString;
  Context->AdjustNumber         = gAdjustNumber;
  Context->SaveChanges          = gSaveChanges;
  Context->OptionMismatch       = gOptionMismatch;
  Context->PromptBlockWidth     = gPromptBlockWidth;
  Context->OptionBlockWidth     = gOptionBlockWidth;
  Context->HelpBlockWidth       = gHelpBlockWidth;
  Context->OldFormSet           = gOldFormSet;
  Context->MenuRefreshHead      = gMenuRefreshHead;

  EfiCopyMem (&Context->ScreenDimensions, &gScreenDimensions, sizeof (gScreenDimensions));
  EfiCopyMem (&Context->Menu, &Menu, sizeof (Menu));

  //
  // Insert to FormBrowser context list
  //
  InsertHeadList (&gBrowserContextList, &Context->Link);
}

VOID
RestoreBrowserContext (
  VOID
  )
/*++

Routine Description:
  Restore globals used by previous call to SendForm().

Arguments:
  None.

Returns:
  None.

--*/
{
  EFI_LIST_ENTRY   *Link;
  BROWSER_CONTEXT  *Context;

  ASSERT (gBrowserContextCount != 0);
  gBrowserContextCount--;
  if (gBrowserContextCount == 0) {
    //
    // This is not reentry of SendForm(), no context to restore
    //
    return;
  }

  ASSERT (!IsListEmpty (&gBrowserContextList));

  Link = GetFirstNode (&gBrowserContextList);
  Context = BROWSER_CONTEXT_FROM_LINK (Link);

  //
  // Restore FormBrowser context
  //
  gBannerData           = Context->BannerData;
  gClassOfVfr           = Context->ClassOfVfr;
  gFunctionKeySetting   = Context->FunctionKeySetting;
  gResetRequired        = Context->ResetRequired;
  gNvUpdateRequired     = Context->NvUpdateRequired;
  gDirection            = Context->Direction;
  gFunctionNineString   = Context->FunctionNineString;
  gFunctionTenString    = Context->FunctionTenString;
  gEnterString          = Context->EnterString;
  gEnterCommitString    = Context->EnterCommitString;
  gEnterEscapeString    = Context->EnterEscapeString;
  gEscapeString         = Context->EscapeString;
  gSaveFailed           = Context->SaveFailed;
  gMoveHighlight        = Context->MoveHighlight;
  gMakeSelection        = Context->MakeSelection;
  gDecNumericInput      = Context->DecNumericInput;
  gHexNumericInput      = Context->HexNumericInput;
  gToggleCheckBox       = Context->ToggleCheckBox;
  gPromptForData        = Context->PromptForData;
  gPromptForPassword    = Context->PromptForPassword;
  gPromptForNewPassword = Context->PromptForNewPassword;
  gConfirmPassword      = Context->ConfirmPassword;
  gConfirmError         = Context->ConfirmError;
  gPassowordInvalid     = Context->PassowordInvalid;
  gPressEnter           = Context->PressEnter;
  gEmptyString          = Context->EmptyString;
  gAreYouSure           = Context->AreYouSure;
  gYesResponse          = Context->YesResponse;
  gNoResponse           = Context->NoResponse;
  gMiniString           = Context->MiniString;
  gPlusString           = Context->PlusString;
  gMinusString          = Context->MinusString;
  gAdjustNumber         = Context->AdjustNumber;
  gSaveChanges          = Context->SaveChanges;
  gOptionMismatch       = Context->OptionMismatch;
  gPromptBlockWidth     = Context->PromptBlockWidth;
  gOptionBlockWidth     = Context->OptionBlockWidth;
  gHelpBlockWidth       = Context->HelpBlockWidth;
  gOldFormSet           = Context->OldFormSet;
  gMenuRefreshHead      = Context->MenuRefreshHead;

  EfiCopyMem (&gScreenDimensions, &Context->ScreenDimensions, sizeof (gScreenDimensions));
  EfiCopyMem (&Menu, &Context->Menu, sizeof (Menu));

  //
  // Remove from FormBrowser context list
  //
  RemoveEntryList (&Context->Link);
  gBS->FreePool (Context);
}
