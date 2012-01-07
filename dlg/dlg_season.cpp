/*
** Taiga, a lightweight client for MyAnimeList
** Copyright (C) 2010-2011, Eren Okka
** 
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../std.h"
#include <algorithm>
#include <ctime>
#include "../animedb.h"
#include "../animelist.h"
#include "../common.h"
#include "dlg_season.h"
#include "../gfx.h"
#include "../myanimelist.h"
#include "../resource.h"
#include "../string.h"
#include "../taiga.h"
#include "../theme.h"
#include "../win32/win_gdi.h"

class SeasonDialog SeasonDialog;

// =============================================================================

SeasonDialog::SeasonDialog() :
  group_by(SEASON_GROUPBY_TYPE), sort_by(SEASON_SORTBY_TITLE)
{
  RegisterDlgClass(L"TaigaSeasonW");
}

BOOL SeasonDialog::OnInitDialog() {
  // Set properties
  SetSizeMin(575, 310);
  SetIconLarge(IDI_MAIN);
  SetIconSmall(IDI_MAIN);

  // Create list
  list_.Attach(GetDlgItem(IDC_LIST_SEASON));
  list_.EnableGroupView(true);
  list_.SetExtendedStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);
  list_.SetTheme();
  SIZE size = {520, 200};
  list_.SetTileViewInfo(0, LVTVIF_FIXEDSIZE, nullptr, &size);
  list_.SetView(LV_VIEW_TILE);

  // Create main toolbar
  toolbar_.Attach(GetDlgItem(IDC_TOOLBAR_SEASON));
  toolbar_.SetImageList(UI.ImgList16.GetHandle(), 16, 16);
  toolbar_.SendMessage(TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_MIXEDBUTTONS);
  // Create search toolbar
  toolbar_filter_.Attach(GetDlgItem(IDC_TOOLBAR_SEARCH));
  toolbar_filter_.SendMessage(TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_MIXEDBUTTONS);
  // Create search text
  edit_.Attach(GetDlgItem(IDC_EDIT_SEASON_FILTER));
  edit_.SetCueBannerText(L"Filter");
  edit_.SetParent(toolbar_filter_.GetWindowHandle());
  edit_.SetPosition(nullptr, 0, 0, 160, 20);
  edit_.SetMargins(1, 16);
  CRect rect_edit; edit_.GetRect(&rect_edit);
  // Create cancel filter button
  cancel_button_.Attach(GetDlgItem(IDC_BUTTON_CANCELFILTER));
  cancel_button_.SetParent(edit_.GetWindowHandle());
  cancel_button_.SetPosition(nullptr, rect_edit.right + 1, 0, 16, 16);

  // Insert toolbar buttons
  BYTE fsStyle1 = BTNS_AUTOSIZE | BTNS_SHOWTEXT;
  BYTE fsStyle2 = BTNS_AUTOSIZE | BTNS_SHOWTEXT | BTNS_WHOLEDROPDOWN;
  toolbar_.InsertButton(0, ICON16_CALENDAR, 100, 1, fsStyle2, 0, L"Select season", nullptr);
  toolbar_.InsertButton(1, ICON16_REFRESH,  101, 1, fsStyle1, 1, L"Refresh data", L"Download anime details and missing images");
  toolbar_.InsertButton(2, 0, 0, 0, BTNS_SEP, 0, nullptr, nullptr);
  toolbar_.InsertButton(3, ICON16_CATEGORY, 103, 1, fsStyle2, 3, L"Group by", nullptr);
  toolbar_.InsertButton(4, ICON16_SORT,     104, 1, fsStyle2, 4, L"Sort by", nullptr);
  toolbar_.InsertButton(5, 0, 0, 0, BTNS_SEP, 0, nullptr, nullptr);
  toolbar_.InsertButton(6, ICON16_BALLOON,  106, 1, fsStyle1, 6, L"Discuss", L"");

  // Create rebar
  rebar_.Attach(GetDlgItem(IDC_REBAR_SEASON));
  // Insert rebar bands
  UINT fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_STYLE;
  UINT fStyle = RBBS_NOGRIPPER;
  rebar_.InsertBand(nullptr, 0, 0, 0, 0, 0, 0, 0, 0, fMask, fStyle);
  rebar_.InsertBand(toolbar_.GetWindowHandle(), GetSystemMetrics(SM_CXSCREEN), 0, 0, 0, 0, 0, 0, 
    HIWORD(toolbar_.GetButtonSize()) + (HIWORD(toolbar_.GetPadding()) / 2), fMask, fStyle);
  rebar_.InsertBand(toolbar_filter_.GetWindowHandle(), 0, 0, 0, 170, 0, 0, 0, 20, fMask, fStyle);

  // Create status bar
  statusbar_.Attach(GetDlgItem(IDC_STATUSBAR_SEASON));
  statusbar_.SetImageList(UI.ImgList16.GetHandle());

  // Refresh
  RefreshData(false);
  RefreshList();
  RefreshStatus();
  RefreshToolbar();

  return TRUE;
}

// =============================================================================

BOOL SeasonDialog::OnCommand(WPARAM wParam, LPARAM lParam) {
  // Toolbar
  switch (LOWORD(wParam)) {
    // Refresh data
    case 101:
      RefreshData(true);
      return TRUE;
    // Discuss
    case 106:
      MAL.ViewSeasonGroup();
      return TRUE;
  }

  // Filter text
  if (HIWORD(wParam) == EN_CHANGE) {
    if (LOWORD(wParam) == IDC_EDIT_SEASON_FILTER) {
      wstring filter_text;
      edit_.GetText(filter_text);
      cancel_button_.Show(filter_text.empty() ? SW_HIDE : SW_SHOWNORMAL);
      RefreshList();
      return TRUE;
    }
  }

  return FALSE;
}

BOOL SeasonDialog::OnDestroy() {
  // Save database
  SeasonDatabase.Save();
#ifdef _DEBUG
  // Save minimal info for release
  SeasonDatabase.Save(L"", true);
#endif
  
  // Free some memory
  images.clear();
  
  return TRUE;
}

LRESULT SeasonDialog::OnNotify(int idCtrl, LPNMHDR pnmh) {
  // List
  if (idCtrl == IDC_LIST_SEASON) {
    return OnListNotify(reinterpret_cast<LPARAM>(pnmh));
  // Toolbar
  } else if (idCtrl == IDC_TOOLBAR_SEASON) {
    return OnToolbarNotify(reinterpret_cast<LPARAM>(pnmh));
  // Button
  } else if (idCtrl == IDC_BUTTON_CANCELFILTER) {
    if (pnmh->code == NM_CUSTOMDRAW) {
      return OnButtonCustomDraw(reinterpret_cast<LPARAM>(pnmh));
    }
  }
  
  return 0;
}

void SeasonDialog::OnSize(UINT uMsg, UINT nType, SIZE size) {
  switch (uMsg) {
    case WM_SIZE: {
      CRect rcWindow;
      rcWindow.Set(0, 0, size.cx, size.cy);
      rcWindow.Inflate(-ScaleX(WIN_CONTROL_MARGIN), -ScaleY(WIN_CONTROL_MARGIN));
      // Resize rebar
      rebar_.SendMessage(WM_SIZE, 0, 0);
      rcWindow.top += rebar_.GetBarHeight() + ScaleY(WIN_CONTROL_MARGIN / 2);
      // Resize status bar
      CRect rcStatus;
      statusbar_.GetClientRect(&rcStatus);
      statusbar_.SendMessage(WM_SIZE, 0, 0);
      rcWindow.bottom -= rcStatus.Height();
      // Resize list
      list_.SetPosition(nullptr, rcWindow);
    }
  }
}

BOOL SeasonDialog::PreTranslateMessage(MSG* pMsg) {
  switch (pMsg->message) {
    case WM_KEYDOWN: {
      if (::GetFocus() == edit_.GetWindowHandle()) {
        switch (pMsg->wParam) {
          // Clear filter text
          case VK_ESCAPE: {
            edit_.SetText(L"");
            return TRUE;
          }
        }
      }
      break;
    }
  }

  return FALSE;
}

LRESULT SeasonDialog::CEditFilter::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    case WM_COMMAND: {
      if (HIWORD(wParam) == BN_CLICKED) {
        // Clear filter text
        if (LOWORD(wParam) == IDC_BUTTON_CANCELFILTER) {
          SetText(L"");
          return TRUE;
        }
      }
      break;
    }
  }
  
  return WindowProcDefault(hwnd, uMsg, wParam, lParam);
}

LRESULT SeasonDialog::OnButtonCustomDraw(LPARAM lParam) {
  LPNMCUSTOMDRAW pCD = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);

  switch (pCD->dwDrawStage) {
    case CDDS_PREPAINT: {
      CDC dc = pCD->hdc;
      dc.FillRect(pCD->rc, ::GetSysColor(COLOR_WINDOW));
      UI.ImgList16.Draw(ICON16_CROSS, dc.Get(), 0, 0);
      dc.DetachDC();
      return CDRF_SKIPDEFAULT;
    }
  }

  return 0;
}

// =============================================================================

LRESULT SeasonDialog::OnListNotify(LPARAM lParam) {
  LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lParam);
  switch (pnmh->code) {
    // Custom draw
    case NM_CUSTOMDRAW: {
      return OnListCustomDraw(lParam);
    }

    // Double click
    case NM_DBLCLK: {
      LPNMITEMACTIVATE lpnmitem = reinterpret_cast<LPNMITEMACTIVATE>(pnmh);
      if (lpnmitem->iItem == -1) break;
      LPARAM param = list_.GetItemParam(lpnmitem->iItem);
      if (param) ExecuteAction(L"ViewAnimePage", 0, param);
      break;
    }

    // Right click
    case NM_RCLICK: {
      LPNMITEMACTIVATE lpnmitem = reinterpret_cast<LPNMITEMACTIVATE>(pnmh);
      if (lpnmitem->iItem == -1) break;
      Anime* anime = reinterpret_cast<Anime*>(list_.GetItemParam(lpnmitem->iItem));
      if (anime) {
        Anime* anime_onlist = AnimeList.FindItem(anime->series_id);
        UpdateSeasonListMenu(anime_onlist == nullptr);
        ExecuteAction(UI.Menus.Show(pnmh->hwndFrom, 0, 0, L"SeasonList"), 0, 
          reinterpret_cast<LPARAM>(anime_onlist ? anime_onlist : anime));
        list_.RedrawWindow();
      }
      break;
    }
  }

  return 0;
}

LRESULT SeasonDialog::OnListCustomDraw(LPARAM lParam) {
  LRESULT result = CDRF_DODEFAULT;
  LPNMLVCUSTOMDRAW pCD = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);

  CDC hdc = pCD->nmcd.hdc;
  CRect rect = pCD->nmcd.rc;

  if (GetWinVersion() < WINVERSION_VISTA) {
    list_.GetSubItemRect(pCD->nmcd.dwItemSpec, pCD->iSubItem, &rect);
  }

  switch (pCD->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: {
      result = CDRF_NOTIFYITEMDRAW;
      break;
    }
    case CDDS_ITEMPREPAINT: {
      result = CDRF_NOTIFYPOSTPAINT;
      break;
    }
    case CDDS_ITEMPOSTPAINT: {
      Anime* anime = reinterpret_cast<Anime*>(pCD->nmcd.lItemlParam);
      if (!anime) break;
      
      // Draw border
      if (GetWinVersion() > WINVERSION_XP) {
        rect.Inflate(-4, -4);
      }
      if (GetWinVersion() < WINVERSION_VISTA && pCD->nmcd.uItemState & CDIS_SELECTED) {
        hdc.FillRect(rect, GetSysColor(COLOR_HIGHLIGHT));
      } else {
        hdc.FillRect(rect, RGB(230, 230, 230));
      }

      // Draw background
      rect.Inflate(-1, -1);
      hdc.FillRect(rect, RGB(250, 250, 250));

      // Calculate text height
      SIZE size = {0};
      GetTextExtentPoint32(hdc.Get(), L"T", 1, &size);
      int text_height = size.cy;

      // Calculate areas
      CRect rect_image(
        rect.left + 4, rect.top + 4, 
        rect.left + 124, rect.bottom - 4);
      CRect rect_title(
        rect_image.right + 4, rect_image.top, 
        rect.right - 4, rect_image.top + 20);
      CRect rect_details(
        rect_title.left + 4, rect_title.bottom + 8, 
        rect_title.right, rect_title.bottom + 8 + (7 * (text_height + 2)));
      CRect rect_synopsis(
        rect_details.left, rect_details.bottom + 4, 
        rect_details.right, rect_image.bottom);

      // Draw image
      int image_index = -1;
      for (size_t i = 0; i < images.size(); i++) {
        if (images.at(i).data == anime->series_id) {
          image_index = static_cast<int>(i);
          break;
        }
      }
      if (image_index > -1 && images.at(image_index).dc.Get()) {
        rect_image = ResizeRect(rect_image, 
          images.at(image_index).width,
          images.at(image_index).height,
          true, true, false);
        hdc.SetStretchBltMode(HALFTONE);
        hdc.StretchBlt(rect_image.left, rect_image.top, 
          rect_image.Width(), rect_image.Height(), 
          images.at(image_index).dc.Get(), 0, 0, 
          images.at(image_index).width, 
          images.at(image_index).height, 
          SRCCOPY);
      }
      
      // Draw title background
      if (true) {
        COLORREF color;
        switch (anime->GetAiringStatus()) {
          case MAL_AIRING:
            color = RGB(225, 245, 231); break;
          case MAL_FINISHED: default:
            color = MAL_LIGHTBLUE; break;
          case MAL_NOTYETAIRED:
            color = RGB(245, 225, 231); break;
        }
        hdc.FillRect(rect_title, color);
      } else {
        Anime* anime_onlist = AnimeList.FindItem(anime->series_id);
        hdc.FillRect(rect_title, anime_onlist ? RGB(225, 245, 231) : MAL_LIGHTBLUE);
      }

      // Draw title
      rect_title.Inflate(-4, 0);
      hdc.EditFont(nullptr, -1, TRUE);
      hdc.SetBkMode(TRANSPARENT);
      hdc.DrawText(anime->series_title.c_str(), anime->series_title.length(), rect_title, 
        DT_END_ELLIPSIS | DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER);

      // Draw details
      int text_top = rect_details.top;
      wstring text;
      #define DRAWLINE(t) \
        text = t; \
        hdc.DrawText(text.c_str(), text.length(), rect_details, \
          DT_END_ELLIPSIS | DT_NOPREFIX | DT_SINGLELINE); \
        rect_details.Offset(0, text_height + 2);

      DRAWLINE(L"Aired:");
      DRAWLINE(L"Episodes:");
      DRAWLINE(L"Genres:");
      DRAWLINE(L"Producers:");
      DRAWLINE(L"Score:");
      DRAWLINE(L"Rank:");
      DRAWLINE(L"Popularity:");

      rect_details.Set(rect_details.left + 75, text_top, 
        rect_details.right, rect_details.top + text_height);
      DeleteObject(hdc.DetachFont());

      text = MAL.TranslateDate(anime->series_start);
      text += anime->series_end != anime->series_start ? L" to " + MAL.TranslateDate(anime->series_end) : L"";
      text += L" (" + MAL.TranslateStatus(anime->GetAiringStatus()) + L")";
      DRAWLINE(text);
      DRAWLINE(MAL.TranslateNumber(anime->series_episodes, L"Unknown"));
      DRAWLINE(anime->genres.empty() ? L"?" : anime->genres);
      DRAWLINE(anime->producers.empty() ? L"?" : anime->producers);
      DRAWLINE(anime->score.empty() ? L"0.00" : anime->score);
      DRAWLINE(anime->rank.empty() ? L"#0" : anime->rank);
      DRAWLINE(anime->popularity.empty() ? L"#0" : anime->popularity);

      #undef DRAWLINE
      
      // Draw synopsis
      rect_synopsis.bottom -= (rect_synopsis.Height() % text_height) + 1;
      text = anime->synopsis;
      hdc.DrawText(text.c_str(), text.length(), rect_synopsis, 
        DT_END_ELLIPSIS | DT_NOPREFIX | DT_WORDBREAK | DT_WORD_ELLIPSIS);

      break;
    }
  }

  hdc.DetachDC();
  return result;
}

LRESULT SeasonDialog::OnToolbarNotify(LPARAM lParam) {
  switch (reinterpret_cast<LPNMHDR>(lParam)->code) {
    // Dropdown button click
    case TBN_DROPDOWN: {
      RECT rect; LPNMTOOLBAR nmt = reinterpret_cast<LPNMTOOLBAR>(lParam);
      ::SendMessage(nmt->hdr.hwndFrom, TB_GETRECT, static_cast<WPARAM>(nmt->iItem), reinterpret_cast<LPARAM>(&rect));          
      MapWindowPoints(nmt->hdr.hwndFrom, HWND_DESKTOP, reinterpret_cast<LPPOINT>(&rect), 2);
      UpdateSeasonMenu();
      wstring action;
      switch (LOWORD(nmt->iItem)) {
        // Select season
        case 100:
          action = UI.Menus.Show(m_hWindow, rect.left, rect.bottom, L"SeasonSelect");
          break;
        // Group by
        case 103:
          action = UI.Menus.Show(m_hWindow, rect.left, rect.bottom, L"SeasonGroup");
          break;
        // Sort by
        case 104:
          action = UI.Menus.Show(m_hWindow, rect.left, rect.bottom, L"SeasonSort");
          break;
      }
      if (!action.empty()) {
        ExecuteAction(action);
      }
      break;
    }

    // Show tooltips
    case TBN_GETINFOTIP: {
      NMTBGETINFOTIP* git = reinterpret_cast<NMTBGETINFOTIP*>(lParam);
      git->cchTextMax = INFOTIPSIZE;
      if (git->hdr.hwndFrom == toolbar_.GetWindowHandle()) {
        git->pszText = const_cast<LPWSTR>(toolbar_.GetButtonTooltip(git->lParam));
      }
      break;
    }
  }

  return 0L;
}

// =============================================================================

void SeasonDialog::RefreshData(bool connect, Anime* anime) {
  size_t size = SeasonDatabase.items.size();
  
  if (!anime && images.size() != size) {
    for (size_t i = 0; i < size; i++) {
      if (i < image_clients_.size()) image_clients_.at(i).Cleanup();
      if (i < info_clients_.size()) info_clients_.at(i).Cleanup();
    }
    if (image_clients_.size() != size) image_clients_.resize(size);
    if (info_clients_.size() != size) info_clients_.resize(size);
    images.clear();
    images.resize(size);
  }

  for (auto i = SeasonDatabase.items.begin(); i != SeasonDatabase.items.end(); ++i) {
    if (anime && anime->series_id != i->series_id) continue;
    size_t index = i - SeasonDatabase.items.begin();
    // Load available image
    images.at(index).data = i->series_id;
    images.at(index).Load(i->GetImagePath());
    // Download missing image
    if (connect && images.at(index).dc.Get() == nullptr) {
      MAL.DownloadImage(&(*i), &image_clients_.at(index));
    }
    // Get details
    if (connect) {
      MAL.SearchAnime(i->series_title, &(*i), &info_clients_.at(index));
    }
  }

  if (connect) {
    SeasonDatabase.modified = true;
    SeasonDatabase.last_modified = time(nullptr);
    RefreshStatus();
  }

  list_.RedrawWindow();
}

void SeasonDialog::RefreshList(bool redraw_only) {
  if (!IsWindow()) return;
  
  if (redraw_only) {
    list_.RedrawWindow();
    return;
  }

  // Set title
  if (SeasonDatabase.name.empty()) {
    SetText(L"Season Browser");
  } else {
    SetText(L"Season Browser - " + SeasonDatabase.name);
  }

  // Disable drawing
  list_.SetRedraw(FALSE);

  // Insert list groups
  list_.RemoveAllGroups();
  list_.EnableGroupView(true); // Required for XP
  switch (group_by) {
    case SEASON_GROUPBY_AIRINGSTATUS:
      for (int i = MAL_AIRING; i <= MAL_NOTYETAIRED; i++) {
        list_.InsertGroup(i, MAL.TranslateStatus(i).c_str(), true, false);
      }
      break;
    case SEASON_GROUPBY_LISTSTATUS:
      for (int i = MAL_NOTINLIST; i <= MAL_PLANTOWATCH; i++) {
        list_.InsertGroup(i, MAL.TranslateMyStatus(i, false).c_str(), true, false);
      }
      break;
    case SEASON_GROUPBY_TYPE:
      for (int i = MAL_TV; i <= MAL_MUSIC; i++) {
        list_.InsertGroup(i, MAL.TranslateType(i).c_str(), true, false);
      }
      break;
  }

  // Filter
  wstring filter_text;
  edit_.GetText(filter_text);
  vector<wstring> filters;
  Split(filter_text, L" ", filters);

  // Add items
  list_.DeleteAllItems();
  for (auto i = SeasonDatabase.items.begin(); i != SeasonDatabase.items.end(); ++i) {
    bool passed_filters = true;
    for (auto j = filters.begin(); j != filters.end(); ++j) {
      if (InStr(i->genres, *j, 0, true) == -1 && 
          InStr(i->producers, *j, 0, true) == -1 && 
          InStr(i->series_title, *j, 0, true) == -1) {
            passed_filters = false;
            break;
      }
    }
    if (!passed_filters) continue;
    int group = -1;
    switch (group_by) {
      case SEASON_GROUPBY_AIRINGSTATUS:
        group = i->GetAiringStatus();
        break;
      case SEASON_GROUPBY_LISTSTATUS: {
        Anime* anime_onlist = AnimeList.FindItem(i->series_id);
        group = anime_onlist ? anime_onlist->GetStatus() : MAL_NOTINLIST;
        break;
      }
      case SEASON_GROUPBY_TYPE:
        group = i->series_type;
        break;
    }
    list_.InsertItem(i - SeasonDatabase.items.begin(), 
      group, -1, 0, nullptr, i->series_title.c_str(), 
      reinterpret_cast<LPARAM>(&(*i)));
  }
  
  // Sort items
  switch (sort_by) {
    case SEASON_SORTBY_AIRINGDATE:
      list_.Sort(0, -1, LIST_SORTTYPE_STARTDATE, ListViewCompareProc);
      break;
    case SEASON_SORTBY_EPISODES:
      list_.Sort(0, -1, LIST_SORTTYPE_EPISODES, ListViewCompareProc);
      break;
    case SEASON_SORTBY_POPULARITY:
      list_.Sort(0, 1, LIST_SORTTYPE_POPULARITY, ListViewCompareProc);
      break;
    case SEASON_SORTBY_SCORE:
      list_.Sort(0, -1, LIST_SORTTYPE_SCORE, ListViewCompareProc);
      break;
    case SEASON_SORTBY_TITLE:
      list_.Sort(0, 1, LIST_SORTTYPE_DEFAULT, ListViewCompareProc);
      break;
  }

  // Redraw
  list_.SetRedraw(TRUE);
  list_.RedrawWindow(nullptr, nullptr, 
    RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void SeasonDialog::RefreshStatus() {
  // Set status
  if (!SeasonDatabase.last_modified) {
    statusbar_.SetText(L"");
  } else {
    time_t time_now = time(nullptr);
    time_t time_last = SeasonDatabase.last_modified;
    if (time_now == time_last) {
      statusbar_.SetText(L"  Last updated: Now");
    } else {
      statusbar_.SetText(L"  Last updated: " + ToDateString(time_now - time_last) + L" ago");
    }
  }
}

void SeasonDialog::RefreshToolbar() {
  toolbar_.EnableButton(1, !SeasonDatabase.items.empty());
  
  wstring text = L"Group by: ";
  switch (group_by) {
    case SEASON_GROUPBY_AIRINGSTATUS:
      text += L"Airing status";
      break;
    case SEASON_GROUPBY_LISTSTATUS:
      text += L"List status";
      break;
    case SEASON_GROUPBY_TYPE:
      text += L"Type";
      break;
  }
  toolbar_.SetButtonText(3, text.c_str());

  text = L"Sort by: ";
  switch (sort_by) {
    case SEASON_SORTBY_AIRINGDATE:
      text += L"Airing date";
      break;
    case SEASON_SORTBY_EPISODES:
      text += L"Episodes";
      break;
    case SEASON_SORTBY_POPULARITY:
      text += L"Popularity";
      break;
    case SEASON_SORTBY_SCORE:
      text += L"Score";
      break;
    case SEASON_SORTBY_TITLE:
      text += L"Title";
      break;
  }
  toolbar_.SetButtonText(4, text.c_str());
}