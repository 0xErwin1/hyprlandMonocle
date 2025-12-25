#include "monocleLayout.hpp"
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/decorations/CHyprGroupBarDecoration.hpp>
#include <format>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>


SMonocleNodeData* CHyprMonocleLayout::getNodeFromWindow(PHLWINDOW pWindow) {
    for (auto& nd : m_lMonocleNodesData) {
        if (nd.pWindow.lock() == pWindow)
            return &nd;
    }

    return nullptr;
}

int CHyprMonocleLayout::getNodesOnWorkspace(const int& ws) {
    int no = 0;
    for (auto& n : m_lMonocleNodesData) {
        if (n.workspaceID == ws)
            no++;
    }

    return no;
}

std::string CHyprMonocleLayout::getLayoutName() {
    return "Monocle";
}

void CHyprMonocleLayout::onWindowCreatedTiling(PHLWINDOW pWindow, eDirection direction) {
    if (pWindow->m_isFloating)
        return;

		const auto WSID = pWindow->workspaceID();
    const auto PWORKSPACE = pWindow->m_workspace;
		
			
    const auto         PMONITOR = pWindow->m_monitor.lock(); 

    auto               OPENINGON = g_pCompositor->m_lastWindow.lock() && g_pCompositor->m_lastWindow.lock()->m_workspace == pWindow->m_workspace ? g_pCompositor->m_lastWindow.lock() : nullptr;

		const auto				MOUSECOORDS = g_pInputManager->getMouseCoordsInternal();
		



		if (g_pInputManager->m_wasDraggingWindow && OPENINGON) {
			if (pWindow->checkInputOnDecos(INPUT_TYPE_DRAG_END, MOUSECOORDS, pWindow))
				return;
		}

    if (OPENINGON && OPENINGON != pWindow && OPENINGON->m_groupData.pNextWindow.lock() // target is group
        && pWindow->canBeGroupedInto(OPENINGON)) {


        static const auto* USECURRPOS = (Hyprlang::INT* const*)g_pConfigManager->getConfigValuePtr("group:insert_after_current");
        (**USECURRPOS ? OPENINGON : OPENINGON->getGroupTail())->insertWindowToGroup(pWindow);

        OPENINGON->setGroupCurrent(pWindow);
        pWindow->applyGroupRules();
        pWindow->updateWindowDecos();
        recalculateWindow(pWindow);
        if(!pWindow->getDecorationByType(DECORATION_GROUPBAR))
			      pWindow->addWindowDeco(makeUnique<CHyprGroupBarDecoration>(pWindow));

        return;
    }

    pWindow->applyGroupRules();
    const auto PNODE = &m_lMonocleNodesData.emplace_front();
	  PNODE->workspaceID = pWindow->workspaceID();
	  PNODE->pWindow = pWindow;
    if (PWORKSPACE->m_hasFullscreenWindow) {
	      g_pCompositor->setWindowFullscreenInternal(PWORKSPACE->getFullscreenWindow(), FSMODE_FULLSCREEN);
    }

    recalculateMonitor(pWindow->monitorID());
	  g_pCompositor->setWindowFullscreenInternal(pWindow, FSMODE_MAXIMIZED);
	  g_pCompositor->focusWindow(pWindow);
}

void CHyprMonocleLayout::onWindowRemovedTiling(PHLWINDOW pWindow) {
    const auto PNODE = getNodeFromWindow(pWindow);

    if (!PNODE)
        return;

    pWindow->unsetWindowData(PRIORITY_LAYOUT);
    pWindow->updateWindowData();


    if (pWindow->isFullscreen())
        g_pCompositor->setWindowFullscreenInternal(pWindow,FSMODE_FULLSCREEN);

    m_lMonocleNodesData.remove(*PNODE);

    recalculateMonitor(pWindow->monitorID());
	  
}

void CHyprMonocleLayout::recalculateMonitor(const MONITORID& monid) {
    const auto PMONITOR   = g_pCompositor->getMonitorFromID(monid);

    if (!PMONITOR)
      return;

    const auto PWORKSPACE = PMONITOR->m_activeWorkspace;

    if (!PWORKSPACE)
        return;

    g_pHyprRenderer->damageMonitor(PMONITOR);

    if (PMONITOR->m_activeSpecialWorkspace) {
        calculateWorkspace(PMONITOR->m_activeSpecialWorkspace);
    }

    // calc the WS
    calculateWorkspace(PWORKSPACE);
}

void CHyprMonocleLayout::calculateWorkspace(PHLWORKSPACE PWORKSPACE) {
    if (!PWORKSPACE)
        return;

    const auto         PMONITOR = g_pCompositor->getMonitorFromID(PWORKSPACE->monitorID());
    if (PWORKSPACE->m_hasFullscreenWindow) {
        if (PWORKSPACE->m_fullscreenMode == FSMODE_FULLSCREEN)
            return;

        // massive hack from the fullscreen func
        const auto      PFULLWINDOW = PWORKSPACE->getFullscreenWindow();

        SMonocleNodeData fakeNode;
        fakeNode.pWindow         = PFULLWINDOW;
        fakeNode.position        = PMONITOR->m_position + PMONITOR->m_reservedTopLeft;
        fakeNode.size            = PMONITOR->m_size - PMONITOR->m_reservedTopLeft - PMONITOR->m_reservedBottomRight;
        fakeNode.workspaceID     = PWORKSPACE->m_id;
        PFULLWINDOW->m_position  = fakeNode.position;
        PFULLWINDOW->m_size      = fakeNode.size;

        applyNodeDataToWindow(&fakeNode);

        return;
    }

	  for(auto &md : m_lMonocleNodesData) {
        if (md.workspaceID != PWORKSPACE->m_id)
			    continue;
		   	md.position = PMONITOR->m_position  + PMONITOR->m_reservedTopLeft + Vector2D(0.0f, 0.0f);
		    md.size = Vector2D(PMONITOR->m_size.x - PMONITOR->m_reservedBottomRight.x - PMONITOR->m_reservedTopLeft.x, PMONITOR->m_size.y - PMONITOR->m_reservedBottomRight.y - PMONITOR->m_reservedTopLeft.y);
		    applyNodeDataToWindow(&md);
  	}
}

void CHyprMonocleLayout::applyNodeDataToWindow(SMonocleNodeData* pNode) {
    PHLMONITOR PMONITOR = nullptr;

    if (g_pCompositor->isWorkspaceSpecial(pNode->workspaceID)) {
        for (auto& m : g_pCompositor->m_monitors) {
            if (m->activeSpecialWorkspaceID() == pNode->workspaceID) {
                PMONITOR = m;
                break;
            }
        }
    } else {
        PMONITOR = g_pCompositor->getWorkspaceByID(pNode->workspaceID)->m_monitor.lock(); 
    }

    if (!PMONITOR) {
        Debug::log(ERR, "Orphaned Node {} (workspace ID: {})!!", static_cast<void *>(pNode), pNode->workspaceID);
        return;
    }

    // for gaps outer
    const bool DISPLAYLEFT   = STICKS(pNode->position.x, PMONITOR->m_position.x + PMONITOR->m_reservedTopLeft.x);
    const bool DISPLAYRIGHT  = STICKS(pNode->position.x + pNode->size.x, PMONITOR->m_position.x + PMONITOR->m_size.x - PMONITOR->m_reservedBottomRight.x);
    const bool DISPLAYTOP    = STICKS(pNode->position.y, PMONITOR->m_position.y + PMONITOR->m_reservedTopLeft.y);
    const bool DISPLAYBOTTOM = STICKS(pNode->position.y + pNode->size.y, PMONITOR->m_position.y + PMONITOR->m_size.y - PMONITOR->m_reservedBottomRight.y);

    const auto PWINDOW = pNode->pWindow.lock();
		const auto WORKSPACERULE = g_pConfigManager->getWorkspaceRuleFor(g_pCompositor->getWorkspaceByID(PWINDOW->workspaceID()));

		if (PWINDOW->isFullscreen() && !pNode->ignoreFullscreenChecks)
			return;

    PWINDOW->unsetWindowData(PRIORITY_LAYOUT);
    PWINDOW->updateWindowData();
		

		


		static auto* const PANIMATE = (Hyprlang::INT* const*)g_pConfigManager->getConfigValuePtr("misc:animate_manual_resizes");

    static auto* const PGAPSINDATA     = (Hyprlang::CUSTOMTYPE* const*)g_pConfigManager->getConfigValuePtr("general:gaps_in");
    static auto* const PGAPSOUTDATA    = (Hyprlang::CUSTOMTYPE* const*)g_pConfigManager->getConfigValuePtr("general:gaps_out");
    auto* const        PGAPSIN         = (CCssGapData*)(*PGAPSINDATA)->getData();
    auto* const        PGAPSOUT        = (CCssGapData*)(*PGAPSOUTDATA)->getData();

    auto               gapsIn  = WORKSPACERULE.gapsIn.value_or(*PGAPSIN);
    auto               gapsOut = WORKSPACERULE.gapsOut.value_or(*PGAPSOUT);



    if (!validMapped(PWINDOW)) {
        Debug::log(ERR, "Node {} holding invalid window {}!!", pNode, PWINDOW);
        return;
    }


    PWINDOW->m_size     = pNode->size;
    PWINDOW->m_position = pNode->position;

    //auto calcPos  = PWINDOW->m_position + Vector2D(*PBORDERSIZE, *PBORDERSIZE);
    //auto calcSize = PWINDOW->m_size - Vector2D(2 * *PBORDERSIZE, 2 * *PBORDERSIZE);

    auto       calcPos  = PWINDOW->m_position;
    auto       calcSize = PWINDOW->m_size;

    const auto OFFSETTOPLEFT = Vector2D((double)(DISPLAYLEFT ? gapsOut.m_left : gapsIn.m_left), (double)(DISPLAYTOP ? gapsOut.m_top : gapsIn.m_top));

    const auto OFFSETBOTTOMRIGHT = Vector2D((double)(DISPLAYRIGHT ? gapsOut.m_right : gapsIn.m_right), (double)(DISPLAYBOTTOM ? gapsOut.m_bottom : gapsIn.m_bottom));

    calcPos  = calcPos + OFFSETTOPLEFT;
    calcSize = calcSize - OFFSETTOPLEFT - OFFSETBOTTOMRIGHT;

    const auto RESERVED = PWINDOW->getFullWindowReservedArea();
    calcPos             = calcPos + RESERVED.topLeft;
    calcSize            = calcSize - (RESERVED.topLeft + RESERVED.bottomRight);

		CBox wb = CBox(calcPos, calcSize);
		wb.round();
      *PWINDOW->m_realSize     = wb.size(); 
      *PWINDOW->m_realPosition = wb.pos(); 
      PWINDOW->sendWindowSize();

    if (m_bForceWarps && !**PANIMATE) {
        g_pHyprRenderer->damageWindow(PWINDOW);

        PWINDOW->m_realPosition->warp();
        PWINDOW->m_realSize->warp();

        g_pHyprRenderer->damageWindow(PWINDOW);
    }

    PWINDOW->updateWindowDecos();

}


void CHyprMonocleLayout::resizeActiveWindow(const Vector2D& pixResize, eRectCorner corner, PHLWINDOW pWindow) {
	return;
}

void CHyprMonocleLayout::fullscreenRequestForWindow(PHLWINDOW pWindow, const eFullscreenMode CURRENT_EFFECTIVE_MODE, const eFullscreenMode EFFECTIVE_MODE) {
    if (!pWindow)
      return;
    const auto PMONITOR   = pWindow->m_monitor.lock(); 
    const auto PWORKSPACE = pWindow->m_workspace;

    // save position and size if floating
    if (pWindow->m_isFloating && CURRENT_EFFECTIVE_MODE == FSMODE_NONE) {
        pWindow->m_lastFloatingSize     = pWindow->m_realSize->goal();
        pWindow->m_lastFloatingPosition = pWindow->m_realPosition->goal();
        pWindow->m_position             = pWindow->m_realPosition->goal();
        pWindow->m_size                 = pWindow->m_realSize->goal();
    }

    if (EFFECTIVE_MODE == FSMODE_NONE) {
        // if it got its fullscreen disabled, set back its node if it had one
        const auto PNODE = getNodeFromWindow(pWindow);
        if (PNODE)
            applyNodeDataToWindow(PNODE);
        else {
            // get back its' dimensions from position and size
            *pWindow->m_realPosition = pWindow->m_lastFloatingPosition;
            *pWindow->m_realSize     = pWindow->m_lastFloatingSize;

            pWindow->unsetWindowData(PRIORITY_LAYOUT);
            pWindow->updateWindowData();
        }
    } else {
        // apply new pos and size being monitors' box
        if (EFFECTIVE_MODE == FSMODE_FULLSCREEN) {
            *pWindow->m_realPosition = PMONITOR->m_position;
            *pWindow->m_realSize     = PMONITOR->m_size;
        } else {
            // This is a massive hack.
            // We make a fake "only" node and apply
            // To keep consistent with the settings without C+P code

            SMonocleNodeData fakeNode;
            fakeNode.pWindow                = pWindow;
            fakeNode.position               = PMONITOR->m_position + PMONITOR->m_reservedTopLeft;
            fakeNode.size                   = PMONITOR->m_size - PMONITOR->m_reservedTopLeft - PMONITOR->m_reservedBottomRight;
            fakeNode.workspaceID            = pWindow->workspaceID();
            pWindow->m_position             = fakeNode.position;
            pWindow->m_size                 = fakeNode.size;
            fakeNode.ignoreFullscreenChecks = true;

            applyNodeDataToWindow(&fakeNode);
        }
    }

    //g_pCompositor->changeWindowZOrder(pWindow, true);
}

void CHyprMonocleLayout::onWindowFocusChange(PHLWINDOW pNewFocus) {
	IHyprLayout::onWindowFocusChange(pNewFocus);
	fullscreenRequestForWindow(pNewFocus, FSMODE_MAXIMIZED, FSMODE_MAXIMIZED);
}



bool CHyprMonocleLayout::isWindowTiled(PHLWINDOW pWindow) {
	return true;
}


void CHyprMonocleLayout::recalculateWindow(PHLWINDOW pWindow) {
    const auto PNODE = getNodeFromWindow(pWindow);

    if (!PNODE)
        return;

    recalculateMonitor(pWindow->monitorID());
}

SWindowRenderLayoutHints CHyprMonocleLayout::requestRenderHints(PHLWINDOW pWindow) {
    // window should be valid, insallah

    SWindowRenderLayoutHints hints;

    return hints; // master doesnt have any hints
}

void CHyprMonocleLayout::switchWindows(PHLWINDOW pWindow, PHLWINDOW pWindow2) {
    // windows should be valid, insallah

    const auto PNODE  = getNodeFromWindow(pWindow);
    const auto PNODE2 = getNodeFromWindow(pWindow2);

	  Debug::log(LOG, "SWITCH WINDOWS {} {}", pWindow, pWindow2);
    if (!PNODE2 || !PNODE)
        return;

	  Debug::log(LOG, "YAY");
    const auto inheritFullscreen = prepareLoseFocus(pWindow);

    if (PNODE->workspaceID != PNODE2->workspaceID) {
        std::swap(pWindow2->m_monitor, pWindow->m_monitor);
        std::swap(pWindow2->m_workspace, pWindow->m_workspace);
    }

    // massive hack: just swap window pointers, lol
    PNODE->pWindow  = pWindow2;
    PNODE2->pWindow = pWindow;

    recalculateMonitor(pWindow->monitorID());
    if (PNODE2->workspaceID != PNODE->workspaceID)
        recalculateMonitor(pWindow2->monitorID());

    g_pHyprRenderer->damageWindow(pWindow);
    g_pHyprRenderer->damageWindow(pWindow2);

    prepareNewFocus(pWindow2, inheritFullscreen);
}

void CHyprMonocleLayout::alterSplitRatio(PHLWINDOW pWindow, float ratio, bool exact) {
    // window should be valid, insallah

	  return;
}


bool CHyprMonocleLayout::prepareLoseFocus(PHLWINDOW pWindow) {
	  Debug::log(LOG, "PREPARE LOSE FOCUS {}", pWindow);
	  return true;
}

void CHyprMonocleLayout::prepareNewFocus(PHLWINDOW pWindow, bool inheritFullscreen) {
    if (!pWindow)
        return;

	  Debug::log(LOG, "PREPARE NEW FOCUS {}", pWindow);
    if (inheritFullscreen)
        g_pCompositor->setWindowFullscreenInternal(pWindow, g_pCompositor->getWorkspaceByID(pWindow->workspaceID())->m_fullscreenMode);
}

std::any CHyprMonocleLayout::layoutMessage(SLayoutMessageHeader header, std::string message) {
    return 0;
}


void CHyprMonocleLayout::moveWindowTo(PHLWINDOW pWindow, const std::string& dir, bool silent) {
    if (!isDirection(dir))
        return;

    const auto PWINDOW2 = g_pCompositor->getWindowInDirection(pWindow, dir[0]);

	  if (!PWINDOW2)
		    return;

	  pWindow->setAnimationsToMove();
	  

		if (pWindow->m_workspace != PWINDOW2->m_workspace) {
 			// if different monitors, send to monitor
			onWindowRemovedTiling(pWindow);
			pWindow->moveToWorkspace(PWINDOW2->m_workspace);
			pWindow->m_monitor = PWINDOW2->m_monitor;
			if (!silent) {
				const auto pMonitor = pWindow->m_monitor.lock();
				g_pCompositor->setActiveMonitor(pMonitor);
			}
			onWindowCreatedTiling(pWindow);
		}
}


void CHyprMonocleLayout::replaceWindowDataWith(PHLWINDOW from, PHLWINDOW to) {
    const auto PNODE = getNodeFromWindow(from);

    if (!PNODE)
        return;

    PNODE->pWindow = to;

    applyNodeDataToWindow(PNODE);
}

void CHyprMonocleLayout::onEnable() {
    for (auto& w : g_pCompositor->m_windows) {
        if (w->m_isFloating || !w->m_isMapped || w->isHidden())
            continue;

        onWindowCreatedTiling(w);
    }
}

void CHyprMonocleLayout::onDisable() {
    m_lMonocleNodesData.clear();
}


Vector2D CHyprMonocleLayout::predictSizeForNewWindowTiled() {
	//What the fuck is this shit. Seriously.
	return {};
}
