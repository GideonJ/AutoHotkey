/*
AutoHotkey

Copyright 2003 Chris Mallett

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "hotkey.h"
#include "globaldata.h"  // For access to g_vk_to_sc and several other global vars.
#include "window.h" // For MsgBox()
//#include "application.h" // For ExitApp()

// Initialize static members:
bool Hotkey::sHotkeysAreActive = false;
HookType Hotkey::sWhichHookNeeded = 0;
HookType Hotkey::sWhichHookActive = 0;
DWORD Hotkey::sTimePrev = {0};
DWORD Hotkey::sTimeNow = {0};
Hotkey *Hotkey::shk[MAX_HOTKEYS] = {NULL};
HotkeyIDType Hotkey::sNextID = 0;
const HotkeyIDType &Hotkey::sHotkeyCount = Hotkey::sNextID;



int Hotkey::AllActivate()
// Returns the total number of hotkeys, both registered and handled by the hooks.
// This function can also be called to install the keyboard hook if the state
// of g_ForceNumLock and such have changed, even if the hotkeys are already
// active.
{
	// In light of the above comment, don't do this:
	//if (sHotkeysAreActive) return sHotkeyCount;
	bool is_neutral;
	modLR_type modifiersLR;
	if (!sHotkeysAreActive) // otherwise, don't do this part because it might mess things up.
	{
		for (int i = 0; i < sHotkeyCount; ++i)
		{
			// For simplicity, don't try to undo keys that are already considered to be
			// handled by the hook, since it's not easy to know if they were set that
			// way using "ForceHotkey, on" or really qualified some other way.
			// Instead, just remove any modifiers that are obviously redundant from all
			// keys (do them all due to cases where RegisterHotkey() fails and the key
			// is then auto-enabled via the hook).  No attempt is currently made to
			// correct a silly hotkey such as "lwin & lwin".  In addition, weird hotkeys
			// such as <^Control and ^LControl are not currently validated and might
			// yield unpredictable results:
			if (modifiersLR = KeyToModifiersLR(shk[i]->mVK, shk[i]->mSC, &is_neutral))
				// This hotkey's action-key is itself a modifier.
				if (is_neutral)
					// Since the action-key is a neutral modifier (not left or right specific),
					// turn off any neutral modifiers that may be on:
					shk[i]->mModifiers &= ~ConvertModifiersLR(modifiersLR);
				else
					shk[i]->mModifiersLR &= ~modifiersLR;
			// HK_MOUSE_HOOK type, and most HK_KEYBD types, are handled by the hotkey constructor.
			// What we do here is change the type of any normal or undetermined key if there are other
			// keys that overlap with it (i.e. because only now are all these keys available for checking).
			if (shk[i]->mType == HK_UNDETERMINED || shk[i]->mType == HK_NORMAL)
			{
				// The idea here is to avoid the use of the keyboard hook if at all possible (since
				// it may reduce system performance slightly).  With that in mind, rather than just
				// forcing NumpadEnter and Enter to be entirely separate keys, both handled by the
				// hook, we allow mod+Enter to take over both keys if their is no mod+NumPadEnter key
				// configured with identical modifiers:
				if (g_vk_to_sc[shk[i]->mVK].b)
					// This hotkey's vk has a counterpart key with the same vk.  Check if either of those
					// two keys exists as a "scan code" hotkey.  If so, This hotkey must be handled
					// by the hook to prevent it from firing for both scan codes.
					// modifiersLR should be zero here because otherwise type would have already been set to use hook.
					if (FindHotkeyBySC(g_vk_to_sc[shk[i]->mVK], shk[i]->mModifiers, shk[i]->mModifiersLR) >= 0)
						// Counterpart key is configured as a hotkey with the same modifiers as this key.
						shk[i]->mType = HK_KEYBD_HOOK;

				// Fall back to default checks if more specific ones above didn't set it:
				if (shk[i]->mType != HK_KEYBD_HOOK)
				{
					// Keys modified by CTRL/SHIFT/ALT/WIN can always be registered normally because these
					// modifiers are never used (are overridden) when that key is used as a ModifierVK
					// for another key.  Example: if key1 is a ModifierVK for another key, ^key1
					// (or any other modified versions of key1) can be registered as a hotkey because
					// that doesn't affect the hook's ability to use key1 as a prefix:
					if (shk[i]->mModifiers)
						shk[i]->mType = HK_NORMAL;
					else
						if ((shk[i]->mVK == VK_LWIN || shk[i]->mVK == VK_RWIN))  // "!shk[i]->mModifiers" already true
							// To prevent the start menu from appearing for a naked LWIN or RWIN, must
							// handle this key with the hook (the presence of a normal modifier makes
							// this unnecessary, at least under WinXP, because the start menu is
							// never invoked when a modifier key is held down with lwin/rwin):
							shk[i]->mType = HK_KEYBD_HOOK;
						else
							// If this hotkey is an unmodified modifier (e.g. control = calc.exe) and
							// there are any other hotkeys that rely specifically on this modifier,
							// have the hook handle this hotkey so that it will only fire on key-up
							// rather than key-down.  Note: cases where this key's modifiersLR or
							// ModifierVK/SC are non-zero -- as well as hotkeys that use sc vs. vk
							// -- have already been set to use the keybd hook, so don't need to be
							// handled here.  UPDATE: All the following cases have been already set
							// to be HK_KEYBD_HOOK:
							// - left/right ctrl/alt/shift (since RegisterHotkey() doesn't support them).
							// - Any key with a ModifierVK/SC
							// - The naked lwin or rwin key (due to the check above)
							// Therefore, the only case left to be detected by this next line is the
							// one in which the user configures the naked neutral key VK_SHIFT,
							// VK_MENU, or VK_CONTROL.  As a safety precaution, always handle those
							// neutral keys with the hook so that their action will only fire
							// when the key is released (thus allowing each key to be used for its
							// normal modifying function):
							if (shk[i]->mVK == VK_SHIFT || shk[i]->mVK == VK_MENU || shk[i]->mVK == VK_CONTROL)
								// In addition, the following are already known to be true or we wouldn't
								// have gotten this far:
								// !shk[i]->mModifiers && !shk[i]->mModifiersLR
								// !shk[i]->mModifierVK && !shk[i]->mModifierSC
								shk[i]->mType = HK_KEYBD_HOOK;
							else
								// Check if this key is used as the modifier (prefix) for any other key.  If it is,
								// the keyboard hook must handle this key also because otherwise the key-down event
								// would trigger the registered hotkey immediately, rather than waiting to see if
								// this key is be held down merely to modify some other key.
								shk[i]->mType = FindHotkeyWithThisModifier(shk[i]->mVK, shk[i]->mSC) >= 0
									? HK_KEYBD_HOOK : HK_NORMAL;
					if (shk[i]->mVK == VK_APPS)
						// Override anything set above:
						// For now, always use the hook to handle hotkeys that use Appskey as a suffix.
						// This is because registering such keys with RegisterHotkey() will fail to suppress
						// hide the key-up events from the system, and the key-up for Apps key, at least in
						// apps like Explorer, is a special event that results in the context menu appearing
						// (though most other apps seem to use the key-down event rather than the key-up,
						// so they would probably work okay).  Note: Of possible future use is the fact that
						// if the Alt key is held down before pressing Appskey, it's native function does
						// not occur.  This may be similar to the fact that LWIN and RWIN don't cause the
						// start menu to appear if a shift key is held down.
						shk[i]->mType = HK_KEYBD_HOOK;
				}
			}

			if (shk[i]->mType == HK_NORMAL)
				if (!shk[i]->Register())
					shk[i]->mType = HK_KEYBD_HOOK;
			if (shk[i]->mType == HK_KEYBD_HOOK)
				sWhichHookNeeded |= HOOK_KEYBD;
			if (shk[i]->mType == HK_MOUSE_HOOK)
				sWhichHookNeeded |= HOOK_MOUSE;
		} // for()
	} // if()

	if (g_ForceNumLock != NEUTRAL || g_ForceCapsLock != NEUTRAL || g_ForceScrollLock != NEUTRAL)
		sWhichHookNeeded |= HOOK_KEYBD;

	if ((sWhichHookNeeded & sWhichHookActive) != sWhichHookNeeded) // bitwise-AND does a set-intersection.
	{
		// At least one of the hooks isn't installed that needs to be.
		// But first check if some other instance has the hook(s) installed.
		static HANDLE hook_mutex = NULL;
		if (!hook_mutex) // otherwise, we already have ownership of the mutex so no need for this check.
		{
			// If script isn't being restarting, shouldn't need much more than one check.
			// If it is being restarted, sometimes the prior instance takes a long time to
			// release the mutex, perhaps due to being partially swapped out (esp. if the
			// prior instance has been running for many hours or days).  UPDATE: This is
			// still displaying a warning sometimes when restarting, even with a 1 second
			// grace/wait period.  So it seems best not to display the warning at all
			// when in restart mode, since obviously (by definition) this script is
			// already running so of course the user wants to restart it unconditionally
			// 99% of the time.  The only exception would be when the user's recent
			// changes to the script (i.e. those for which the restart is being done)
			// now require one of the hooks that hadn't been required before (rare).
			// So for now, when in restart mode, just acquire the mutex but don't display
			// any warning if another instance also has the mutex:
			hook_mutex = CreateMutex(NULL, TRUE, NAME_P "Hooks");
			if (!g_ForceLaunch && !g_script.mIsRestart && GetLastError() == ERROR_ALREADY_EXISTS)
			{
				int result = MsgBox("Another instance of this program already has the keyboard or"
					" mouse hook installed (because some of its hotkeys require the hook(s))."
					"  Installing them a second time might produce unexpected behavior.  Do it anyway?"
					"\n\nYou can disable this warning by starting the program with /force as a parameter."
					, MB_YESNO);
				if (result != IDYES)
					g_script.ExitApp();
				// Note: It's not necessary to ever close the Mutex with CloseHandle() because:
				// "The system closes the handle automatically when the process terminates.
				// The mutex object is destroyed when its last handle has been closed."
			}
		}
		sWhichHookActive = HookInit(shk, sHotkeyCount, sWhichHookNeeded);
		if ((sWhichHookNeeded & HOOK_KEYBD) && (sWhichHookNeeded & HOOK_MOUSE) && !sWhichHookActive)
		{
			MsgBox("HookInit() failed to activate both the keyboard and mouse hook.");
			return -1;
		}
		if ((sWhichHookNeeded & HOOK_KEYBD) && !(sWhichHookActive & HOOK_KEYBD))
		{
			MsgBox("HookInit() failed to activate the keyboard hook.");
			return -1;
		}
		if ((sWhichHookNeeded & HOOK_MOUSE) && !(sWhichHookActive & HOOK_MOUSE))
		{
			MsgBox("HookInit() failed to activate the mouse hook.");
			return -1;
		}
	}

	sHotkeysAreActive = true;
	return sHotkeyCount;
}



ResultType Hotkey::AllDeactivate()
// Returns OK or FAIL (but currently not failure modes).
{
	if (!sHotkeysAreActive)
		return OK;
	if (sWhichHookActive)
		HookTerm();
	for (int i = 0; i < sHotkeyCount; ++i)
		shk[i]->Unregister();
	sHotkeysAreActive = false;
	return OK;
}



ResultType Hotkey::AllDestruct()
// Returns OK or FAIL (but currently not failure modes).
{
	AllDeactivate();
	for (int i = 0; i < sHotkeyCount; ++i)
		delete shk[i];  // unregisters before destroying
	sNextID = 0;
	return OK;
}



void Hotkey::AllDestructAndExit(int aExitCode)
{
	AllDestruct();
	// Do this only at the last possible moment prior to exit() because otherwise
	// it may free memory that is still in use by objects that depend on it.
	// This is actually kinda wrong because when exit() is called, the destructors
	// of static, global, and main-scope objects will be called.  If any of these
	// destructors try to reference memory freed() by DeleteAll(), there could
	// be trouble.
	// It's here mostly for traditional reasons.  I'm 99.99999 percent sure that there would be no
	// penalty whatsoever to omitting this, since any modern OS will reclaim all
	// memory dynamically allocated upon program termination.  Indeed, omitting
	// deletes and free()'s for simple objects will often improve the reliability
	// and performance since the OS is far more efficient at reclaiming the memory
	// than us doing it manually (which involves a potentially large number of deletes
	// due to all the objects and sub-objects to be destructed in a typical C++ program).
	// UPDATE: In light of the first paragraph above, it seems best not to do this at all,
	// instead letting all implicitly-called destructors run prior to program termination,
	// at which time the OS will reclaim all remaining memory:
	//SimpleHeap::DeleteAll();

	// In light of the comments below, and due to the fact that anyone using this app
	// is likely to want the anti-focus-stealing measure to always be disabled, I
	// think it's best not to bother with this ever, since its results are
	// unpredictable:
/*	if (g_os.IsWin98orLater() || g_os.IsWin2000orLater())
		// Restore the original timeout value that was set by WinMain().
		// Also disables the compiler warning for the PVOID cast.
		// Note: In many cases, this call will fail to set the value (perhaps even if
		// SystemParametersInfo() reports success), probably because apps aren't
		// supposed to change this value unless they currently have the input
		// focus or something similar (and this app probably doesn't meet the criteria
		// at this stage).  So I think what will happen is: the value set
		// in WinMain() will stay in effect until the user reboots, at which time
		// the default value store in the registry will once again be in effect.
		// This limitation seems harmless.  Indeed, it's probably a good thing not to
		// set it back afterward so that windows behave more consistently for the user
		// regardless of whether this program is currently running.
#ifdef _MSC_VER
	#pragma warning( disable : 4312 )
#endif
		SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (PVOID)g_OriginalTimeout, SPIF_SENDCHANGE);
#ifdef _MSC_VER
	#pragma warning( default : 4312 ) 
#endif
*/
	exit(aExitCode);
}



ResultType Hotkey::PerformID(HotkeyIDType aHotkeyID)
// Returns OK, FAIL, or CRITICAL_ERROR.
{
	// Currently, hotkey_id can't be < 0 due to its type, so we only check if it's too large:
	if (aHotkeyID >= sHotkeyCount)
	{
		MsgBox("Received a hotkey ID larger than the configured range!");
		return FAIL;  // Not a critical error in case some other app is sending us bogus messages?
	}

	// Help prevent runaway hotkeys (infinite loops due to recursion in bad config files):
	static UINT throttled_key_count = 0;  // This var doesn't belong in struct since it's used only here.
	UINT time_until_now;
	int display_warning;
	if (!sTimePrev)
		sTimePrev = GetTickCount();

	if (shk[aHotkeyID]->mJumpToLabel != NULL)  // Probably safest to throttle all others.
	{
		++throttled_key_count;
		sTimeNow = GetTickCount();
		// Calculate the amount of time since the last reset of the sliding interval.
		// Note: A tickcount in the past can be subtracted from one in the future to find
		// the true difference between them, even if the system's uptime is greater than
		// 49 days and the future one has wrapped but the past one hasn't.  This is
		// due to the nature of DWORD math.  The only time this calculation will be
		// unreliable is when the true difference between the past and future
		// tickcounts itself is greater than about 49 days:
		time_until_now = (sTimeNow - sTimePrev);
		if (display_warning = (throttled_key_count > (DWORD)g_MaxHotkeysPerInterval
			&& time_until_now < (DWORD)g_HotkeyThrottleInterval))
		{
			// The moment any dialog is displayed, hotkey processing is halted since this
			// app currently has only one thread.
			char error_text[2048];
			// Using %f with wsprintf() yields a floating point runtime error dialog.
			// UPDATE: That happens if you don't cast to float, or don't have a float var
			// involved somewhere.  Avoiding floats altogether may reduce EXE size
			// and maybe other benefits (due to it not being "loaded")?
			snprintf(error_text, sizeof(error_text), "More than %u hotkeys have been received in the last %ums."
				"This could indicate a runaway condition (infinite loop) due to conflicting keys"
				" within the config file (usually due to the Send command).  It might be possible to"
				" fix this problem simply by including the $ prefix in the hotkey definition"
				" (e.g. $!d::), which would install the keyboard hook to handle this hotkey.\n\n"
				" In addition, this warning can be reduced or eliminated by adding the following lines"
				" anywhere in the script:\n"
				"#HotkeyInterval %d  ; Increase this value slightly to reduce the problem.\n"
				"#MaxHotkeysPerInterval %d  ; Decreasing this value (milliseconds) should also help.\n\n"
				" Do you want to continue (choose NO to exit the program)?"  // In case its stuck in a loop.
				, g_MaxHotkeysPerInterval, g_HotkeyThrottleInterval
				, g_MaxHotkeysPerInterval, g_HotkeyThrottleInterval);
			// This is now needed since hotkeys can still fire while a messagebox is displayed:
			g_IgnoreHotkeys = true;
			if (MsgBox(error_text, MB_YESNO) == IDNO)
				g_script.ExitApp();
			g_IgnoreHotkeys = false;
		}
		// The display_warning var is needed due to the fact that there's an OR in this condition:
		if (display_warning || time_until_now > (DWORD)g_HotkeyThrottleInterval)
		{
			// Reset the sliding interval whenever it expires.  Doing it this way makes the
			// sliding interval more sensitive than alternate methods might be.
			// Also reset it if a warning was displayed, since in that case it didn't expire.
			throttled_key_count = 0;
			sTimePrev = sTimeNow;
		}
		if (display_warning)
			// At this point, even though the user chose to continue, it seems safest
			// to ignore this particular hotkey event since it might be WinClose or some
			// other command that would have unpredictable results due to the displaying
			// of the dialog itself.
			return OK;
	}
	return shk[aHotkeyID]->Perform();
}



ResultType Hotkey::AddHotkey(Label *aJumpToLabel, HookActionType aHookAction)
// Return OK or FAIL.
{
	if (NULL == (shk[sNextID] = new Hotkey(sNextID, aJumpToLabel, aHookAction)))
		return FAIL;
	if (!shk[sNextID]->mConstructedOK) // This really shouldn't happen?.
	{
		delete shk[sNextID];
		return FAIL;
	}
	++sNextID;
	return OK;
}



Hotkey::Hotkey(HotkeyIDType aID, Label *aJumpToLabel, HookActionType aHookAction) // Constructor
	: mID(HOTKEY_ID_INVALID)  // Default until overridden.
	, mVK(0)
	, mSC(0)
	, mModifiers(0)
	, mModifiersLR(0)
	, mAllowExtraModifiers(false)
	, mDoSuppress(true)
	, mModifierVK(0)
	, mModifierSC(0)
	, mModifiersConsolidated(0)
	, mType(HK_UNDETERMINED)
	, mIsRegistered(false)
	, mHookAction(aHookAction)
	, mJumpToLabel(aJumpToLabel)
	, mConstructedOK(false)

// It's better to receive the hotkey_id as a param, since only the caller has better knowledge and
// verification of the fact that this hotkey's id is always set equal to it's index in the array
// (for performance reasons).
{
	// Don't allow hotkeys to be added while the set is already active.  This avoids complications such as having
	// to activate one of the hooks if not already active, and having to pass new hotkey config to the DLL.
	// In addition, it avoids the problem where a key already registered as a hotkey is assigned to become
	// a prefix (handled by the hook).  The registration (if without shift/alt/win/ctrl modifiers) would prevent
	// the hook from ever seeing the key.
	if (sHotkeysAreActive) return;
	if (aID > HOTKEY_ID_MAX) return;   // Probably should never happen.
	if (aJumpToLabel == NULL) return;  // Even for alt-tab, should have the label just for record-keeping.

	if (sHotkeyCount >= MAX_HOTKEYS)
	{
		MsgBox("The maximum number of hotkeys has been reached.  Some have not been loaded.");
		return;  // Success since the above is just a warning.
	}

	if (!TextInterpret()) // The called function already displayed the error.
		return;

	char error_text[512];
	if (   (mHookAction == HOTKEY_ID_ALT_TAB || mHookAction == HOTKEY_ID_ALT_TAB_SHIFT)
		&& !mModifierVK && !mModifierSC   )
	{
		if (mModifiers)
		{
			// Neutral modifier has been specified.  Future enhancement: improve this
			// to try to guess which key, left or right, should be used based on the
			// location of the suffix key on the keyboard.
			snprintf(error_text, sizeof(error_text), "Warning: The following hotkey is AltTab but has a"
				" neutral modifying prefix key.  For this type, you must specify left"
				" or right by using something like:\n\n"
				"RWIN" COMPOSITE_DELIMITER "RShift::AltTab\n"
				"or\n"
				">+Rwin::AltTab"
				"\n\nThis hotkey has not been enabled:\n%s"
				, mJumpToLabel->mName);
			MsgBox(error_text);
			return;  // Key is invalid so don't give it an ID.
		}
		if (mModifiersLR)
		{
			switch (mModifiersLR)
			{
			case MOD_LCONTROL: mModifierVK = g_os.IsWin9x() ? VK_CONTROL : VK_LCONTROL; break;
			case MOD_RCONTROL: mModifierVK = g_os.IsWin9x() ? VK_CONTROL : VK_RCONTROL; break;
			case MOD_LSHIFT: mModifierVK = g_os.IsWin9x() ? VK_SHIFT : VK_LSHIFT; break;
			case MOD_RSHIFT: mModifierVK = g_os.IsWin9x() ? VK_SHIFT : VK_RSHIFT; break;
			case MOD_LALT: mModifierVK = g_os.IsWin9x() ? VK_MENU : VK_LMENU; break;
			case MOD_RALT: mModifierVK = g_os.IsWin9x() ? VK_MENU : VK_RMENU; break;
			case MOD_LWIN: mModifierVK = VK_LWIN; break; // Win9x should support LWIN/RWIN.
			case MOD_RWIN: mModifierVK = VK_RWIN; break;
			default:
				snprintf(error_text, sizeof(error_text), "Warning: The following hotkey is AltTab but has"
					" more than one modifying prefix key, which is not allowed."
					"  This hotkey has not been enabled:\n%s"
					, mJumpToLabel->mName);
				MsgBox(error_text);
				return;  // Key is invalid so don't give it an ID.
			}
			// Since above didn't return:
			mModifiersLR = 0;  // Since ModifierVK/SC is now its substitute.
		}
		// Update: This is no longer needed because the hook attempts to compensate.
		// However, leaving it enabled may improve performance and reliability.
		// Update#2: No, it needs to be disabled, otherwise alt-tab won't work right
		// in the rare case where an ALT key itself is defined as "AltTabMenu":
		//else
			// It has no ModifierVK/SC and no modifiers, so it's a hotkey that is defined
			// to fire only when the Alt-Tab menu is visible.  Since the ALT key must be
			// down for that menu to be visible (on all OSes?), add the ALT key to this
			// keys modifiers so that it will be detected as a hotkey whenever the
			// Alt-Tab menu is visible:
		//	modifiers |= MOD_ALT;
	}

	if (mType != HK_MOUSE_HOOK)  // Don't let a mouse key ever be affected by these checks.
		if (g_ForceKeybdHook == TOGGLED_ON || mModifiersLR || mAllowExtraModifiers || !mDoSuppress || aHookAction)
			mType = HK_KEYBD_HOOK;

	// Currently, these take precedence over each other in the following order, so don't
	// just bitwise-or them together in case there's any ineffectual stuff stored in
	// the fields that have no effect (e.g. modifiers have no effect if there's a mModifierVK):
	if (mModifierVK)
		mModifiersConsolidated = KeyToModifiersLR(mModifierVK);
	else if (mModifierSC)
		mModifiersConsolidated = KeyToModifiersLR(0, mModifierSC);
	else
	{
		mModifiersConsolidated = mModifiersLR;
		if (mModifiers)
			mModifiersConsolidated |= ConvertModifiers(mModifiers);
	}

	// Always assign the ID last, right before a successful return, so that the caller is notified
	// that the constructor succeeded:
	mConstructedOK = true;
	mID = aID;
	// Don't do this because the caller still needs the old/unincremented value:
	//++sHotkeyCount;  // Hmm, seems best to do this here, but revisit this sometime.
}



ResultType Hotkey::TextInterpret()
// Returns OK or FAIL.
{
	char *term2;
	if (   !(term2 = stristr(mJumpToLabel->mName, COMPOSITE_DELIMITER))   )
		return TextToKey(TextToModifiers(mJumpToLabel->mName), false) ? OK : FAIL;
    char *end_of_term1 = omit_trailing_whitespace(mJumpToLabel->mName, term2) + 1;
	// Temporarily terminate the string so that the 2nd term is hidden:
	char ctemp = *end_of_term1;
	*end_of_term1 = '\0';
	int result = TextToKey(mJumpToLabel->mName, true);
	*end_of_term1 = ctemp;  // Undo the termination.
	if (!result)
		return FAIL;
	term2 += strlen(COMPOSITE_DELIMITER);
	term2 = omit_leading_whitespace(term2);
	// Even though modifiers on keys already modified by a mModifierVK are not supported, call
	// TextToModifiers() anyway to use its output (for consistency).  The modifiers it sets
	// are currently ignored because the mModifierVK takes precedence.
	return TextToKey(TextToModifiers(term2), false) ? OK : FAIL;
}



char *Hotkey::TextToModifiers(char *aText)
// Takes input param <text> to support receiving only a subset of object.text.
// Returns the location in <text> of the first non-modifier key.
// Checks only the first char(s) for modifiers in case these characters appear elsewhere (e.g. +{+}).
// But come to think of it, +{+} isn't valid because + itself is already shift-equals.  So += would be
// used instead, e.g. +==action.  Similarly, all the others, except =, would be invalid as hotkeys also.
{
	if (!aText) return aText;
	if (!*aText) return aText;

	// Explicitly avoids initializing modifiers to 0 because the caller may have already included
	// some set some modifiers in there.
	char *marker;
	bool key_left, key_right;
	for (marker = aText, key_left = key_right = false; *marker; ++marker)
	{
		switch (*marker)
		{
		case '>':
			key_right = true;
			break;
		case '<':
			key_left = true;
			break;
		case '*':
			mAllowExtraModifiers = true;
			break;
		case '~':
			mDoSuppress = false;
			break;
		case '$':
			mType = HK_KEYBD_HOOK;
			break;
		case '!':
			if ((!key_right && !key_left))
			{
				mModifiers |= MOD_ALT;
				break;
			}
			// Both left and right may be specified, e.g. ><+a means both shift keys must be held down:
			if (key_left)
			{
				mModifiersLR |= MOD_LALT;
				key_left = false;
			}
			if (key_right)
			{
				mModifiersLR |= MOD_RALT;
				key_right = false;
			}
			break;
		case '^':
			if ((!key_right && !key_left))
			{
				mModifiers |= MOD_CONTROL;
				break;
			}
			if (key_left)
			{
				mModifiersLR |= MOD_LCONTROL;
				key_left = false;
			}
			if (key_right)
			{
				mModifiersLR |= MOD_RCONTROL;
				key_right = false;
			}
			break;
		case '+':
			if ((!key_right && !key_left))
			{
				mModifiers |= MOD_SHIFT;
				break;
			}
			if (key_left)
			{
				mModifiersLR |= MOD_LSHIFT;
				key_left = false;
			}
			if (key_right)
			{
				mModifiersLR |= MOD_RSHIFT;
				key_right = false;
			}
			break;
		case '#':
			if ((!key_right && !key_left))
			{
				mModifiers |= MOD_WIN;
				break;
			}
			if (key_left)
			{
				mModifiersLR |= MOD_LWIN;
				key_left = false;
			}
			if (key_right)
			{
				mModifiersLR |= MOD_RWIN;
				key_right = false;
			}
			break;
		default:
			return marker;  // Return immediately whenever a non-modifying char is found.
		}
	}
	return marker;
}



int Hotkey::TextToKey(char *aText, bool aIsModifier)
// Takes input param aText to support receiving only a subset of this.text.
// In private members, sets the values of vk/sc or ModifierVK/ModifierSC depending on aIsModifier.
// It may also merge new modifiers into the existing value of modifiers, so the caller
// should never reset modifiers after calling this.
// Returns non-zero on success.
{
	char error_text[512];
	if (!aText || !*aText) return 0;

	// Init in case of early return:
	if (aIsModifier)
		mModifierVK = mModifierSC = 0;
	else
		mVK = mSC = 0;

	vk_type temp_vk = 0;
	sc_type temp_sc = 0;
	mod_type modifiers = 0;
	modLR_type modifiersLR = 0;
	bool is_mouse = false;
	if (temp_vk = TextToVK(aText, &modifiers, true))
	{
		if (aIsModifier && (temp_vk == VK_WHEEL_DOWN || temp_vk == VK_WHEEL_UP))
		{
			// Can't display mJumpToLabel->mName because our caller temporarily truncated it in the middle:
			snprintf(error_text, sizeof(error_text), "Warning: \"%s\" is not allowed to be used as a prefix key."
				"  This hotkey has not been enabled.", aText);
			MsgBox(error_text);
			return 0;
		}
		is_mouse = VK_IS_MOUSE(temp_vk);
		if (modifiers & MOD_SHIFT)
			if (temp_vk >= 'A' && temp_vk <= 'Z')  // VK of an alpha char is the same as the ASCII code of its uppercase version.
				modifiers &= ~MOD_SHIFT;  // Making alpha chars case insensitive seems much more friendly.
	}
	else // no VK was found
		if (   !(temp_sc = TextToSC(aText))   )
		{
			snprintf(error_text, sizeof(error_text), "Warning: \"%s\" is an invalid key name."
				"  This hotkey has not been enabled.", aText);
			MsgBox(error_text);
			return 0;
		}


/*
If ever do this, be sure to avoid doing it for keys that must be tracked by scan code (e.g. those in the
scan code array).
	if (!temp_vk && !is_mouse)  // sc must be non-zero or else it would have already returned above.
		if (temp_vk = g_sc_to_vk[temp_sc])
		{
			snprintf(error_text, sizeof(error_text), "DEBUG: \"%s\" (scan code %X) was successfully mapped to virtual key %X", text, temp_sc, temp_vk);
			MsgBox(error_text);
			temp_sc = 0; // Maybe set this just for safety, even though a non-zero vk should always take precedence over it.
		}
*/
	if (is_mouse)
		mType = HK_MOUSE_HOOK;

	if (aIsModifier)
	{
		mModifierVK = temp_vk;
		mModifierSC = temp_sc;
		if (!is_mouse)
			mType = HK_KEYBD_HOOK;  // Always use the hook for keys that have a mModifierVK or SC
		return mModifierVK || mModifierSC;
	}
	else
	{
		mVK = temp_vk;
		mSC = temp_sc;
		mModifiers |= modifiers;  // Turn on any additional modifiers.  e.g. SHIFT to realize '#'.
		mModifiersLR |= modifiersLR;
		if (!is_mouse)
			if (   !temp_vk || temp_vk == VK_NUMLOCK || temp_vk == VK_CAPITAL || temp_vk == VK_SCROLL
				|| temp_vk == VK_LCONTROL || temp_vk == VK_RCONTROL
				|| temp_vk == VK_LSHIFT || temp_vk == VK_RSHIFT
				|| temp_vk == VK_LMENU || temp_vk == VK_RMENU   )
				// Scan codes having no available virtual key must always be handled by the hook.
				// In addition, to support preventing the toggleable keys from toggling, handle those
				// with the hook also.  Finally, the non-neutral (left-right) modifier keys must
				// also be done with the hook because even if RegisterHotkey() claims to succeed
				// on them, I'm 99% sure I tried it and the hotkeys don't really work.
				mType = HK_KEYBD_HOOK;
		return mVK || mSC;
	}
}



ResultType Hotkey::Register()
// Returns OK or FAIL.
{
	if (mIsRegistered) return OK;
	// Can't use the API method to register such hotkeys.  They are handled by the hook:
	if (mType != HK_NORMAL) return FAIL;

	// Indicate that the key modifies itself because RegisterHotkey() requires that +SHIFT,
	// for example, be used to register the naked SHIFT key.  So what we do here saves the
	// user from having to specify +SHIFT in the config file:
	mod_type modifiers_prev = mModifiers;
	switch (mVK)
	{
	case VK_LWIN:
	case VK_RWIN: mModifiers |= MOD_WIN; break;
	case VK_CONTROL: mModifiers |= MOD_CONTROL; break;
	case VK_SHIFT: mModifiers |= MOD_SHIFT; break;
	case VK_MENU: mModifiers |= MOD_ALT; break;
	}

/*
	if (   !(mIsRegistered = RegisterHotKey(NULL, id, modifiers, vk))   )
		// If another app really is already using this hotkey, there's no point in trying to have the keyboard
		// hook try to handle it instead because registered hotkeys take precedence over keyboard hook.
		// UPDATE: For WinNT/2k/XP, this warning can be disabled because registered hotkeys can be
		// overridden by the hook.  But something like this is probably needed for Win9x:
		char error_text[MAX_EXEC_STRING];
		snprintf(error_text, sizeof(error_text), "RegisterHotKey() of hotkey \"%s\" (id=%d, virtual key=%d, modifiers=%d) failed,"
			" perhaps because another application (or Windows itself) is already using it."
			"  You could try adding the line \"%s, On\" prior to its line in the config file."
			, text, id, vk, modifiers, g_cmd[CMD_FORCE_KEYBD_HOOK]);
		MsgBox(error_text);
		return FAIL;
	return 1;
*/
	// Must register them to our main window (i.e. don't use NULL to indicate our thread),
	// otherwise any modal dialogs, such as MessageBox(), that call DispatchMessage()
	// internally wouldn't be able to find anyone to send hotkey messages to, so they
	// would probably be lost:
	if (mIsRegistered = RegisterHotKey(g_hWnd, mID, mModifiers, mVK))
		return OK;

	// On failure, reset the modifiers in case this function changed them.  This is done
	// in case this hotkey will now be handled by the hook, which doesn't want any
	// extra modifiers that were added above:
	mModifiers = modifiers_prev;
	return FAIL;
}



ResultType Hotkey::Unregister()
// Returns OK or FAIL.
{
	if (!mIsRegistered) return OK;
	// Don't report any errors in here, at least not when we were called in conjunction
	// with cleanup and exit.  Such reporting might cause an infinite loop, leading to
	// a stack overflow if the reporting itself encounters an error and tries to exit,
	// which in turn would call us again:
	if (mIsRegistered = !UnregisterHotKey(g_hWnd, mID))  // I've see it fail in one rare case.
		return FAIL;
	return OK;
}



int Hotkey::FindHotkeyBySC(sc2_type aSC2, mod_type aModifiers, modLR_type aModifiersLR)
// Returns the the HotkeyID if found, -1 otherwise.
// Answers the question: What is the first hotkey with the given sc & modifiers *regardless* of
// any non-zero mModifierVK or mModifierSC it may have?  The mModifierSC/vk is ignored because
// the caller wants to know whether this key would be blocked if its counterpart were registered.
// For example, the hook wouldn't see "MEDIA_STOP & NumpadENTER" at all if NumPadENTER was
// already registered via RegisterHotkey(), since RegisterHotkey() doesn't honor any modifiers
// other than the standard ones.
{
	for (int i = 0; i < sHotkeyCount; ++i)
		if (!shk[i]->mVK && (shk[i]->mSC == aSC2.a || shk[i]->mSC == aSC2.b))
			if (shk[i]->mModifiers == aModifiers && shk[i]->mModifiersLR == aModifiersLR)  // Ensures an exact match.
				return i;
	return -1;  // No match found.
}



int Hotkey::FindHotkeyWithThisModifier(vk_type aVK, sc_type aSC)
// Returns the the HotkeyID if found, -1 otherwise.
// Answers the question: What is the first hotkey with mModifierVK or mModifierSC equal to those given?
// A non-zero vk param will take precendence over any non-zero value for sc.
{
	if (!aVK & !aSC) return -1;
	for (int i = 0; i < sHotkeyCount; ++i)
		if (   (aVK && aVK == shk[i]->mModifierVK) || (aSC && aSC == shk[i]->mModifierSC)   )
			return i;
	return -1;  // No match found.
}



int Hotkey::FindHotkeyContainingModLR(modLR_type aModifiersLR) // , int hotkey_id_to_omit)
// Returns the the HotkeyID if found, -1 otherwise.
// Find the first hotkey whose modifiersLR contains *any* of the modifiers shows in the parameter value.
// The caller tells us the ID of the hotkey to omit from the search because that one
// would always be found (since something like "lcontrol=calc.exe" in the config file
// would really be defines as  "<^control=calc.exe".
// Note: By intent, this function does not find hotkeys whose normal/neutral modifiers
// contain <modifiersLR>.
{
	if (!aModifiersLR) return -1;
	for (int i = 0; i < sHotkeyCount; ++i)
		// Bitwise set-intersection: indicates if anything in common:
		if (shk[i]->mModifiersLR & aModifiersLR)
		//if (i != hotkey_id_to_omit && shk[i]->mModifiersLR & modifiersLR)
			return i;
	return -1;  // No match found.
}



char *Hotkey::ListHotkeys(char *aBuf, size_t aBufSize)
// Translates this script's list of variables into text equivalent, putting the result
// into aBuf and returning the position in aBuf of its new string terminator.
{
	if (!aBuf || aBufSize < 256) return NULL;
	char *aBuf_orig = aBuf;
	// Save vertical space by limiting newlines here:
	snprintf(aBuf, BUF_SPACE_REMAINING, "Type\tName\r\n"
							 "----------------------------------\r\n");
	aBuf += strlen(aBuf);
	// Start at the oldest and continue up through the newest:
	for (int i = 0; i < sHotkeyCount; ++i)
		aBuf = shk[i]->ToText(aBuf, BUF_SPACE_REMAINING, true);
	return aBuf;
}



char *Hotkey::ToText(char *aBuf, size_t aBufSize, bool aAppendNewline)
// Translates this var into its text equivalent, putting the result into aBuf and
// returning the position in aBuf of its new string terminator.
{
	if (!aBuf) return NULL;
	char *aBuf_orig = aBuf;
	snprintf(aBuf, BUF_SPACE_REMAINING, "%s%s\t%s"
		, (mType == HK_KEYBD_HOOK) ? "k-hook" : ((mType == HK_MOUSE_HOOK) ? "m-hook" : "reg")
		, (mType == HK_NORMAL && !mIsRegistered) ? "(no)" : ""
		, mJumpToLabel->mName);
	aBuf += strlen(aBuf);
	if (aAppendNewline && BUF_SPACE_REMAINING >= 2)
	{
		*aBuf++ = '\r';
		*aBuf++ = '\n';
		*aBuf = '\0';
	}
	return aBuf;
}
