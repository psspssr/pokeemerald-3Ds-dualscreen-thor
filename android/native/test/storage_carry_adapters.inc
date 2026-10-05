/* Drawing/animation adapters; all persistent record mutations are real code. */
#define tState data[0]
#define tSelectedOption data[1]
#define tInput data[2]
#define tNextOption data[3]
#define tWindowId data[15]
#define STORAGE_ACK(keys) JOY_NEW(keys)
static struct Sprite cursorSprite, itemSprites[3];
static struct { bool8 pending; u8 callback, area, pos; } itemAnimations[3];
static const u8 description[] = {0xBB, EOS};
static const struct { const u8 *desc; } sMainMenuTexts[5] = {
    {description}, {description}, {description}, {description}, {description},
};
const u8 gText_PartyFull[] = {EOS}, gText_JustOnePkmn[] = {EOS};
static const u8 *printedMessage;
static bool8 weatherReady, offeredSummary;
static s8 yesNoAnswer;
static unsigned noticePrints;

static void ClearSavedCursorPos(void) {}
static void CreateCursorSprites(void) { sStorage->cursorSprite = &cursorSprite; }
static void TryRefreshDisplayMon(void) {}
static void ReshowDisplayMon(void) {}
static void CreateMovingMonIcon(void) {}
static void SetMovingMonSprite(u8 mode, u8 pos) {}
static void SetPlacedMonSprite(u8 box, u8 pos) {}
static void SetDisplayMonData(void *mon, u8 mode) {}
static void DestroyMovingMonIcon(void) {}
static void DestroyPartyMonIcon(u8 pos) {}
static void CreateBoxMonIconAtPos(u8 pos) {}
static void DestroyReleaseMonIcon(void) {}
static void InitCanReleaseMonVars(void) {}
static void InitReleaseMon(void) {}
static s8 RunCanReleaseMon(void) { return FALSE; }
static bool8 TryHideReleaseMon(void) { return FALSE; }
static void RefreshDisplayMonData(void) {}
static void CompactPartySprites(void) {}
static u8 GetNumPartySpritesCompacting(void) { return 0; }
static void RefreshDisplayMon(void) {}
static void StartDisplayMonMosaicEffect(void) {}
static void UpdatePartySlotColors(void) {}
static void ReshowReleaseMon(void) {}
static bool8 ResetReleaseMonSpritePtr(void) { return FALSE; }
static void TrySetCursorFistAnim(void) {}
void StartSpriteAnim(struct Sprite *sprite, u8 anim) {}
bool8 BeginNormalPaletteFade(u32 mask, s8 delay, u8 start, u8 end, u16 color) { return TRUE; }
u8 UpdatePaletteFade(void) { return 0; }
static void SetMenuText(u8 id) { if (id == MENU_SUMMARY) offeredSummary = TRUE; }
static u16 GetSpeciesAtCursorPosition(void)
{
    if (sCursorArea == CURSOR_AREA_IN_PARTY)
        return GetMonData(&gPlayerParty[sCursorPosition], MON_DATA_SPECIES);
    return GetCurrentBoxMonData(sCursorPosition, MON_DATA_SPECIES);
}

static void CreateItemIconSprites(void)
{
    memset(sStorage->itemIcons, 0, sizeof(sStorage->itemIcons));
    memset(itemAnimations, 0, sizeof(itemAnimations));
    for (unsigned i = 0; i < MAX_ITEM_ICONS; i++)
        sStorage->itemIcons[i].sprite = &itemSprites[i];
    sStorage->movingItemId = ITEM_NONE;
}
static void SetItemIconAffineAnim(u8 id, u8 anim) { assert(id < MAX_ITEM_ICONS); }
static void SetItemIconCallback(u8 id, u8 callback, u8 area, u8 pos)
{
    assert(id < MAX_ITEM_ICONS);
    itemAnimations[id].pending = TRUE;
    itemAnimations[id].callback = callback;
    itemAnimations[id].area = area;
    itemAnimations[id].pos = pos;
}
static void SetItemIconPosition(u8 id, u8 area, u8 pos)
{
    assert(id < MAX_ITEM_ICONS);
    sStorage->itemIcons[id].area = area;
    sStorage->itemIcons[id].pos = pos;
}
static void finishItemAnimations(void)
{
    for (unsigned i = 0; i < MAX_ITEM_ICONS; i++)
    {
        if (!itemAnimations[i].pending) continue;
        u8 cb = itemAnimations[i].callback;
        if (cb == ITEM_CB_WAIT_ANIM)
            sStorage->itemIcons[i].active = FALSE;
        else if (cb == ITEM_CB_TO_HAND || cb == ITEM_CB_SWAP_TO_HAND)
            SetItemIconPosition(i, CURSOR_AREA_IN_HAND, 0);
        else
            SetItemIconPosition(i, itemAnimations[i].area, itemAnimations[i].pos);
        itemAnimations[i].pending = FALSE;
    }
}
static void SetBoxMonIconObjMode(u8 pos, u8 mode) {}
static void SetPartyMonIconObjMode(u8 pos, u8 mode) {}
static void PlaySE(u16 song) {}
static void PrintMessage(u8 id) {}
static void ShowYesNoWindow(s8 cursor) {}
static void ClearBottomWindow(void) {}
static void Task_PokeStorageMain(u8 taskId) {}
static s8 StorageYesNoInput(void) { return yesNoAnswer; }
static bool8 IsItemIconAnimActive(void) { return FALSE; }
static void StartCursorAnim(u8 anim) {}
static bool8 IsDma3ManagerBusyWithBgCopy(void) { return FALSE; }

static void CreateMainMenu(u8 selected, s16 *window) { *window = 2; }
void LoadMessageBoxAndBorderGfx(void) {}
void DrawDialogueFrame(u8 window, bool8 copy) {}
u16 AddTextPrinterParameterized2(u8 window, u8 font, const u8 *text, u8 speed,
    void (*callback)(struct TextPrinterTemplate *, u16), u8 fg, u8 bg, u8 shadow)
{
    printedMessage = text;
    noticePrints++;
    return 0;
}
void CopyWindowToVram(u8 window, u8 mode) {}
static bool8 IsWeatherNotFadingIn(void) { return weatherReady; }
s8 Menu_ProcessInput(void) { return MENU_NOTHING_CHOSEN; }
void ClearStdWindowAndFrame(u8 window, bool8 copy) {}
static void UnlockPlayerFieldControls(void) {}
static void ScriptContext_Enable(void) {}
void RemoveWindow(u8 window) {}
static void FadeScreen(u8 mode, s8 delay) {}
u8 Menu_MoveCursor(s8 delta) { return 0; }
u8 Menu_GetCursorPos(void) { return 0; }
static void CleanupOverworldWindowsAndTilemaps(void) {}
u8 GetItemPocket(u16 item) { return item == ITEM_NONE ? POCKET_NONE : POCKET_ITEMS; }
static u8 CurrentBattlePyramidLocation(void) { return PYRAMID_LOCATION_NONE; }
static bool8 FlagGet(u16 flag) { return FALSE; }
bool8 AddPyramidBagItem(u16 item, u16 count) { assert(0); return FALSE; }
