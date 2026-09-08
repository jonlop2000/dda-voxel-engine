#pragma once

#include <string_view>

#include "UI/Runtime/UiElement.h"

namespace engine::game
{
struct CollectionCodexView;
}

namespace ui
{

inline constexpr std::string_view kCollectionCodexScreenId = "ui_collection_codex";
inline constexpr std::string_view kOpenCollectionCodexAction =
    "open_collection_codex";
inline constexpr std::string_view kCloseCollectionCodexAction =
    "close_collection_codex";
inline constexpr std::string_view kSelectCollectionCodexSpeciesAction =
    "select_collection_codex_species";
inline constexpr std::string_view kCollectionCodexButtonId = "collection_codex_open";
inline constexpr std::string_view kCollectionCodexCloseButtonId =
    "collection_codex_close";
inline constexpr std::string_view kCollectionCodexListContentId =
    "collection_codex_list_content";
inline constexpr std::string_view kCollectionCodexDetailContentId =
    "collection_codex_detail_content";

enum class CollectionCodexScrollRegion
{
    None,
    SpeciesList,
    Detail,
};

UiTree makeCollectionCodexScreenTree();
bool setCollectionCodexViewport(UiTree& tree, float width, float height);
bool setCollectionCodexView(UiTree& tree,
                            const engine::game::CollectionCodexView& view,
                            std::string_view selectedSpeciesId = {});

// the tree must have a current computed layout before pointer coordinates are
// queried. the returned region is retained-tree based, so callers do not need
// to duplicate the codex panel geometry.
CollectionCodexScrollRegion collectionCodexScrollRegionAt(const UiTree& tree,
                                                           float x, float y);

// applies a row-sized, clamped wheel step to whichever scrollable codex region
// contains the pointer. returns true when the event belongs to a codex scroll
// region, including when that region is already at its scroll boundary.
bool scrollCollectionCodexAt(UiTree& tree, float x, float y,
                             double wheelYOffset);

} // namespace ui
