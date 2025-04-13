file(GLOB COMPONENT_SOURCES "*.c")
file(GLOB DSP_IIR_SOURCES "../../managed_components/espressif__esp-dsp/modules/iir/biquad/*.c")
file(GLOB DSP_IIR_INCLUDES "../../managed_components/espressif__esp-dsp/modules/iir/include/")
file(GLOB DSP_COMMON_INCLUDES "../../managed_components/espressif__esp-dsp/modules/common/include/")
idf_component_register(
		SRCS
			${DSP_IIR_SOURCES}
            ${COMPONENT_SOURCES}
        INCLUDE_DIRS 
        	"include"
        	 "../../managed_components/espressif__esp-dsp/modules/iir/include"
        	 "../../managed_components/espressif__esp-dsp/modules/common/include/"
        	 
        REQUIRES 
        	audio_stream
        	audio_pipeline
        	audio_sal
        	audio_board
        	sntp_client
)