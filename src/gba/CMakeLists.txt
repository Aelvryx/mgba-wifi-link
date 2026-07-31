include(ExportDirectory)
set(SOURCE_FILES
	../gb/audio.c
	audio.c
	bios.c
	cart/ereader.c
	cart/gpio.c
	cart/matrix.c
	cart/unlicensed.c
	cart/vfame.c
	cheats.c
	cheats/codebreaker.c
	cheats/gameshark.c
	cheats/parv3.c
	core.c
	dma.c
	gba.c
	hle-bios.c
	input.c
	io.c
	memory.c
	overrides.c
	replica.c
	replicated-pair.c
	renderers/cache-set.c
	renderers/common.c
	renderers/gl.c
	renderers/software-bg.c
	renderers/software-mode0.c
	renderers/software-obj.c
	renderers/video-software.c
	savedata.c
	serialize.c
	sharkport.c
	sio.c
	sio/gbp.c
	timer.c
	video.c)

set(NETPLAY_FILES
	sio/netplay/driver.c
	sio/netplay/identity.c
	sio/netplay/protocol.c
	sio/netplay/protocol-v2.c
	sio/netplay/session.c
	sio/netplay/session-v2.c
	sio/netplay/timeline.c
	sio/netplay/transport.c)

set(LOCKSTEP_FILES
	sio/lockstep.c)

set(SIO_FILES
	sio/dolphin.c
	${LOCKSTEP_FILES}
	${NETPLAY_FILES})

set(EXTRA_FILES
	extra/battlechip.c
	extra/proxy.c)

set(DEBUGGER_FILES
	debugger/cli.c)

set(TEST_FILES
	test/cheats.c
	test/core.c
	test/netplay-driver.c
	test/netplay-identity.c
	test/netplay-integration.c
	test/netplay-protocol.c
	test/netplay-protocol-v2.c
	test/netplay-session.c
	test/netplay-session-v2.c
	test/netplay-transport.c
	test/replica.c
	test/replicated-pair.c
	test/replicated-pair-spike.c
	test/sio.c)

source_group("GBA board" FILES ${SOURCE_FILES})
source_group("GBA extras" FILES ${EXTRA_FILES} ${SIO_FILES})
source_group("GBA debugger" FILES ${DEBUGGER_FILES})
source_group("GBA tests" FILES ${TEST_FILES})

export_directory(GBA SOURCE_FILES)
export_directory(GBA_LOCKSTEP LOCKSTEP_FILES)
export_directory(GBA_SIO SIO_FILES)
export_directory(GBA_NETPLAY NETPLAY_FILES)
export_directory(GBA_EXTRA EXTRA_FILES)
export_directory(GBA_DEBUGGER DEBUGGER_FILES)
export_directory(GBA_TEST TEST_FILES)
