# Changelog

## Unreleased
### Fixed
- Fix corruption in jamming (thanks [@maakmusic])

## v0.1.5 (2019-02-11)
### Added
- Add mix settings
- [#56] - Add installation rules and loader for translations (thanks [@jpcima])
- Load module from command line argument
- [#57] - Add FM envelope paste from plain text like MML (thanks [@pedipanol])
- Support .y12 and .ins instrument file importing
- [#58] - Implement retrieving channel state during playback (thanks [@papiezak])
- Add effect description in status bar

### Fixed
- [#48] - Fix incorrect label when setting buffer length to 1ms (thanks [@OPNA2608])
- [#51] - Add png icon (thanks [@ehaupt])
- [#52], [#54] - Fix install instructions for FreeBSD, Debian and Ubuntu (thanks [@ehaupt], [@alexmyczko])
- [#55] - Change default octave up/down keys (thanks [@galap-1])
- Fix instrument deep clone corruption by deregistering FM envelope user (thanks [@maakmusic])
- Fix editable step to apply entering events and be "0" selectable (thanks [@maakmusic])
- Fix to unmute track on jam mode (thanks [@maakmusic])
- [#44], [#59] - Divide instrument owned check from its number (thanks [@papiezak], [@djtuBIG-MaliceX])
- Fix echo buffer size and FM envelope reset before echo buffer access
- Change default FM envelope reset state to unused
- Fix wait time of VGM
- [#60] - Fix loop point of S98 (thanks [@scarletbullgon])
- Fix to work FM TL4 sequence macro (thanks [@maakmusic])
- Fix to skip instrument redefinition
- Fix FM TL3 calculation in instrument definition event

[#48]: https://github.com/rerrahkr/BambooTracker/issues/48
[#51]: https://github.com/rerrahkr/BambooTracker/issues/51
[#52]: https://github.com/rerrahkr/BambooTracker/pull/52
[#54]: https://github.com/rerrahkr/BambooTracker/pull/54
[#55]: https://github.com/rerrahkr/BambooTracker/issues/55
[#56]: https://github.com/rerrahkr/BambooTracker/pull/56
[#57]: https://github.com/rerrahkr/BambooTracker/issues/57
[#44]: https://github.com/rerrahkr/BambooTracker/issues/44
[#59]: https://github.com/rerrahkr/BambooTracker/issues/59
[#58]: https://github.com/rerrahkr/BambooTracker/issues/58
[#60]: https://github.com/rerrahkr/BambooTracker/issues/60

[@ehaupt]: https://github.com/ehaupt
[@galap-1]: https://github.com/galap-1
[@djtuBIG-MaliceX]: https://github.com/djtuBIG-MaliceX
[@scarletbullgon]: https://github.com/scarletbullgon

## v0.1.4 (2019-01-01)
### Added
- [#27] - Add application icon (thanks [@Decidetto])
- [#29], [#36], [#41] - Create BambooTracker.desktop (thanks [@alexmyczko], [@trebmuh], [@OPNA2608])
- [#30] - Create BambooTracker.1 (thanks [@alexmyczko], [@OPNA2608])
- [#37], [#38] - Support WOPN bank and instrument format (thanks [@jpcima])
- Add S98 export
- [#13] - Add selection to move the cursor to right in the effect columns (thanks [@jimbo1qaz])
- [#13] - Add highlighted step jump shortcuts (thanks [@jimbo1qaz])
- Add general settings descriptions in configuration dialog
- [#13] - Add editable step (thanks [@jimbo1qaz])
- Support SCCI (beta) ([#46], [#47]; thanks [@papiezak])
- Add tool bar in instrument list

### Fixed
- [#28] - Fix error when compiling before Qt5.10 (thanks [@papiezak] and others)
- [#33] - Revise README (thanks [@OPNA2608])
- [#31] - Replacing Json config with QSettings ([#17]; thanks [@OPNA2608])
- [#39] - Fix invalid memory access on main window destruction (thanks [@jpcima])
- Optimize wait data of exported VGM and S98
- [#40] - Change FM octave lower (thanks [@papiezak])
- Fix to set the current instrument when entering instrument number in pattern editor
- [#45] - Update README Linux build instructions, Fix typo in config dialogue (thanks [@OPNA2608])
- Rewrite about dialog

[#27]: https://github.com/rerrahkr/BambooTracker/issues/27
[#28]: https://github.com/rerrahkr/BambooTracker/issues/28
[#29]: https://github.com/rerrahkr/BambooTracker/pull/29
[#30]: https://github.com/rerrahkr/BambooTracker/pull/30
[#33]: https://github.com/rerrahkr/BambooTracker/pull/33
[#36]: https://github.com/rerrahkr/BambooTracker/pull/36
[#31]: https://github.com/rerrahkr/BambooTracker/pull/31
[#17]: https://github.com/rerrahkr/BambooTracker/issues/17
[#37]: https://github.com/rerrahkr/BambooTracker/pull/37
[#38]: https://github.com/rerrahkr/BambooTracker/pull/38
[#39]: https://github.com/rerrahkr/BambooTracker/pull/39
[#41]: https://github.com/rerrahkr/BambooTracker/pull/41
[#13]: https://github.com/rerrahkr/BambooTracker/issues/13
[#40]: https://github.com/rerrahkr/BambooTracker/issues/40
[#45]: https://github.com/rerrahkr/BambooTracker/pull/45
[#46]: https://github.com/rerrahkr/BambooTracker/issues/46
[#47]: https://github.com/rerrahkr/BambooTracker/pull/47

[@Decidetto]: https://github.com/Decidetto
[@alexmyczko]: https://github.com/alexmyczko
[@trebmuh]: https://github.com/trebmuh
[@jpcima]: https://github.com/jpcima

## v0.1.3 (2018-12-14)
### Added
- [#18] - Add FM operator copy/paste (thanks [@jimbo1qaz])
- [#14] - Assign instrument editor close shortcut to escape key (thanks [@jimbo1qaz])
- [#26] - Add menu shortcuts (thanks [@jimbo1qaz])
- [#2] - Add simple key shortcut settings (thanks [@pedipanol])

### Fixed
- Fix default place of load/save instrument
- [#25] - Change FM synthesis frequency from 110933Hz to 55466Hz (thanks [@papiezak])
- [#6], [#14] - Fix to set new property when adding instrument (thanks [@ScoopJohn], [@jimbo1qaz])
- [#2] - Fix wheel step in slider (thanks [@pedipanol])
- Fix file load/save to use local encoding (thanks [@maakmusic])
- Fix loop point check when exporting to wav and vgm (thanks [@nabetaqz])
- Fix support mod/inst file v1.0.1
- [#24] - Fix crash when adding song (thanks [@JonKaruzu])
- Fix focus in instrument list
- [#26] - Fix mute actions in pattern editor (thanks [@jimbo1qaz])
- [#26] - Fix pattern and order context menu shortcuts (thanks [@jimbo1qaz])

[#25]: https://github.com/rerrahkr/BambooTracker/issues/25
[#14]: https://github.com/rerrahkr/BambooTracker/issues/14
[#18]: https://github.com/rerrahkr/BambooTracker/issues/18
[#24]: https://github.com/rerrahkr/BambooTracker/issues/24
[#26]: https://github.com/rerrahkr/BambooTracker/issues/26

[@papiezak]: https://github.com/papiezak

## v0.1.2 (2018-12-02)
### Added
- [#2], [#6] - Add selection whether the order of FM volume is reversed (thanks [@pedipanol], [@ScoopJohn])
- [#22] - Add .dmp, .tfi and .vgi instrument file importing support ([#2], [#6]; thanks [@Pigu-A])
- [#12] - Add order/pattern focus shortcut (thanks [@jimbo1qaz])

### Fixed
- Fix module load error by missing pattern size initialization (thanks [@maakmusic])
- [#10] - Rearrange tab stops for better keyboard navigation (thanks [@Pigu-A])
- [#11] - Change envelope reset behavior (thanks [@Pigu-A])
- Fix instrument property used/unused inheritance by cloning
- [#20] - Split FM envelope copy/paste to multiple lines (thanks [@jimbo1qaz])
- [#21] - Fix selection range in pattern editor (thanks [@jimbo1qaz])
- [#1] - Make compatible with WinXP (thanks [@JonKaruzu])
- [#2] - Fix the order of input for columns (thanks [@pedipanol])
- [#23] - Fix tick event to be loaded from playing step (thanks [@pedipanol])
- Fix play/stop song when pressing return key
- [#12] - Fix insert/clone order selection when clicking above or below orders (thanks [@jimbo1qaz])
- Fix clone order to assign first free patterns

[#10]: https://github.com/rerrahkr/BambooTracker/pull/10
[#11]: https://github.com/rerrahkr/BambooTracker/pull/16
[#20]: https://github.com/rerrahkr/BambooTracker/pull/20
[#21]: https://github.com/rerrahkr/BambooTracker/issues/21
[#1]: https://github.com/rerrahkr/BambooTracker/issues/1
[#2]: https://github.com/rerrahkr/BambooTracker/issues/2
[#6]: https://github.com/rerrahkr/BambooTracker/issues/6
[#23]: https://github.com/rerrahkr/BambooTracker/issues/23
[#22]: https://github.com/rerrahkr/BambooTracker/pull/22
[#12]: https://github.com/rerrahkr/BambooTracker/issues/12

[@maakmusic]: https://twitter.com/maakmusic
[@Pigu-A]: https://github.com/Pigu-A
[@jimbo1qaz]: https://github.com/jimbo1qaz
[@JonKaruzu]: https://github.com/JonKaruzu
[@pedipanol]: https://github.com/pedipanol
[@ScoopJohn]: https://github.com/ScoopJohn

## v0.1.1 (2018-11-27)
### Added
- [#8] - Add dependencies and compile instructions on linux to readme (thanks [@OPNA2608])

### Fixed
- [#5] - Fix scroll directions of scroll bar arrow in instrument sequence editor (thanks [@Xyz39808])
- [#4] - Fix wrong offset jump when opening file with operator sequence (thanks [@9001], [@nabetaqz])
- [#9] - Check if saved soundOutput exists ([#7]; thanks [@OPNA2608], [@marysiamzawka])

[#5]: https://github.com/rerrahkr/BambooTracker/issues/5
[#4]: https://github.com/rerrahkr/BambooTracker/issues/4
[#9]: https://github.com/rerrahkr/BambooTracker/pull/9
[#7]: https://github.com/rerrahkr/BambooTracker/issues/7
[#8]: https://github.com/rerrahkr/BambooTracker/issues/8

[@Xyz39808]: https://github.com/Xyz39808
[@9001]: https://github.com/9001
[@OPNA2608]: https://github.com/OPNA2608
[@marysiamzawka]: https://github.com/marysiamzawka
[@nabetaqz]: https://twitter.com/nabetaqz

## v0.1.0 (2018-11-25)
### Added
- First release
