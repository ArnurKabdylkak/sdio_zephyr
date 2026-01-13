
module WishboneController(
    output reg sd_clock,
    inout sd_command,
    inout[3:0] sd_data,
    output[3:0] sd_debug,
    output wlan_wake_host,
    output wlan_enable,

    input wb_clk,
    input wb_rst,

    input wb_cyc_i,
    input wb_stb_i,
    input wb_we_i,
    input[29:0] wb_adr_i,
    input[31:0] wb_dat_w_i,
    input[3:0] wb_sel_i,
    output reg[31:0] wb_dat_o,
    output wb_ack_o,
    output wb_err_o
);

	/*
	assign sd_debug[0] = sd_clock;
	assign sd_debug[1] = sd_command;
	assign sd_debug[2] = sd_data[0];
	assign sd_debug[3] = wb_rst;
    */
    assign sd_debug[0] = sd_clock;
	assign sd_debug[1] = sd_command;
	assign sd_debug[2] = sd_data[0];
	assign sd_debug[3] = sd_data[1];
	
	assign wlan_wake_host = 1'b1; // todo: remove unrelated ports
	assign wlan_enable = 1'b1;
    
    
    reg[31:0] MAIN_CLOCK_FREQUENCY = 32'd48_000_000;
    reg[31:0] SD_CLOCK_FREQUENCY = 32'd100_000;

    localparam MAIN_CLOCK_FREQUENCY_POINTER = 32'h0000000;
    localparam SD_CLOCK_FREQUENCY_POINTER = 32'h00001000;
    localparam CMD_INDEX_POINTER = 32'h00002000;
    localparam CMD_ARGUMENT_POINTER = 32'h00003000;
    localparam DATA_BUFFER_POINTER = 32'h00004000;

    localparam SEND_CMD_OP_POINTER = 32'h00005000;
    localparam SEND_CMD_AND_READ_DATA_OP_POINTER = 32'h00006000;
    localparam SEND_CMD_AND_SEND_DATA_OP_POINTER = 32'h00007000;
    localparam READ_DATA_OP_POINTER = 32'h00008000;
    localparam SEND_DATA_OP_POINTER = 32'h00009000;
    
    localparam CMD_BUSY_POINTER = 32'h0000a000;
    localparam DATA_BUSY_POINTER = 32'h0000b000;
    
    localparam CMD_STATUS_POINTER = 32'h0000c000;
    localparam DATA_STATUS_POINTER = 32'h0000d000;
    
    localparam DATA_LENGTH_POINTER = 32'h0000e000;

    wire read_clock = wb_clk;
    wire[8:0] read_address = wb_adr_i[8:0];
    wire[31:0] read_data;

	wire write_clock = wb_clk;
	reg write_enable = 1'b0;
	reg[8:0] write_address = 9'd0; 
	reg[31:0] write_data = 32'd0;


    reg commandStartFlag = 1'b0;    
    reg dataStartFlag = 1'b0;
    reg dataWriteEnableFlag = 1'b0;

    reg dataValid = 1'b0;
    
    reg wb_ack_o_buf = 1'b0;
    reg wb_err_o_buf = 1'b0;
    assign wb_ack_o = wb_ack_o_buf & wb_stb_i & wb_cyc_i;
    assign wb_err_o = wb_err_o_buf & wb_stb_i & wb_cyc_i;
    wire commandBusy = commandState != CMD_IDLE || commandStartFlag == 1'b1;
    wire dataBusy = dataState != DATA_IDLE || dataStartFlag == 1'b1;
    

    always @(posedge wb_clk) begin
        if(!wb_rst) begin
            write_enable <= 1'b0;
            dataValid <= 1'b0;
            if(commandState == CMD_PROCESSING || commandState == CMD_DONE)  
            	commandStartFlag <= 1'b0;
            if(dataState == DATA_PROCESSING || dataState == DATA_DONE)
            	dataStartFlag <= 1'b0;
            wb_ack_o_buf <= 1'b0;
            wb_err_o_buf <= 1'b0;
            if(wb_cyc_i && wb_stb_i) begin
            	if(wb_we_i) begin
                	case({wb_adr_i, 2'b0} & 32'h0000F000)
                  		MAIN_CLOCK_FREQUENCY_POINTER : begin
                        	MAIN_CLOCK_FREQUENCY <= wb_dat_w_i;
                            wb_ack_o_buf <= 1'b1;
                      	end
                        SD_CLOCK_FREQUENCY_POINTER : begin
                        	SD_CLOCK_FREQUENCY <= wb_dat_w_i;
                           	wb_ack_o_buf <= 1'b1;
                    	end
                      	CMD_INDEX_POINTER : begin
                       		commandRequestIndex <= wb_dat_w_i[5:0];
                       	  	wb_ack_o_buf <= 1'b1;
                       	end
                    	CMD_ARGUMENT_POINTER : begin
                        	commandRequestArgument <= wb_dat_w_i;
                           	wb_ack_o_buf <= 1'b1;
                      	end 
                       	DATA_BUFFER_POINTER : begin
                       		write_enable <= 1'b1;
                         	write_address <= wb_adr_i[8:0];
                           	write_data <= wb_dat_w_i;
                         	wb_ack_o_buf <= 1'b1;
                     	end
                     	DATA_LENGTH_POINTER : begin
							dataLength <= wb_dat_w_i[10:0];
                     		wb_ack_o_buf <= 1'b1;
                     	end 
                       	 
                     	default : begin
                       		wb_err_o_buf <= 1'b1;
                      	end
                  	endcase 
              	end else begin
                	case({wb_adr_i, 2'b0} & 32'h0000F000)
                    	MAIN_CLOCK_FREQUENCY_POINTER : begin
                        	wb_dat_o <= MAIN_CLOCK_FREQUENCY;
                           	wb_ack_o_buf <= 1'b1;
                      	end
                      	SD_CLOCK_FREQUENCY_POINTER : begin
                        	wb_dat_o <= SD_CLOCK_FREQUENCY;
                          	wb_ack_o_buf <= 1'b1;
                      	end
                     	CMD_INDEX_POINTER : begin
                      		wb_dat_o <= {24'b0, commandResponseIndex};
                     		wb_ack_o_buf <= 1'b1;
                       	end
                   		CMD_ARGUMENT_POINTER : begin
                        	case(wb_adr_i[1:0])
                         		2'b00 : wb_dat_o <= commandResponseArgument[31:0];
                            	2'b01 : wb_dat_o <= commandResponseArgument[63:32];
                             	2'b10 : wb_dat_o <= commandResponseArgument[95:64];
                            	2'b11 : wb_dat_o <= {12'b0, commandResponseArgument[119:96]};
                        	endcase
                          	wb_ack_o_buf <= 1'b1;
                    	end 
                      	DATA_BUFFER_POINTER : begin
                        	dataValid <= 1'b1;
                        	if(dataValid) begin
                           		wb_dat_o <= read_data;
                         		wb_ack_o_buf <= 1'b1;
                         	end
                  		end
                  		SEND_CMD_OP_POINTER : begin
                  			if(!commandBusy) begin
                        		commandStartFlag <= 1'b1;
                        		wb_dat_o <= 32'b0;
                        	end else begin
                        		wb_dat_o <= {31'b0, commandBusy};                        		
                        	end
                         	wb_ack_o_buf <= 1'b1;
                       	end
                       	SEND_CMD_AND_READ_DATA_OP_POINTER : begin
                       		if(!commandBusy && !dataBusy) begin
                        		commandStartFlag <= 1'b1;
                            	dataStartFlag <= 1'b1;
                            	dataWriteEnableFlag <= 1'b0;
                            	wb_dat_o <= 32'd0;
                            end begin
                            	wb_dat_o <= {30'd0, dataBusy, commandBusy};
                            end
                            wb_ack_o_buf <= 1'b1;
                       	end 
                       	SEND_CMD_AND_SEND_DATA_OP_POINTER : begin
                       		if(!commandBusy && !dataBusy) begin
                        		commandStartFlag <= 1'b1;
                            	dataStartFlag <= 1'b1;
                            	dataWriteEnableFlag <= 1'b1;
                            	wb_dat_o <= 32'd0;
                            end begin
                            	wb_dat_o <= {30'd0, dataBusy, commandBusy};
                            end
                            wb_ack_o_buf <= 1'b1;
                       	end 
                     	READ_DATA_OP_POINTER : begin
                     		if(!dataBusy) begin
                     			dataStartFlag <= 1'b1;
                     			dataWriteEnableFlag <= 1'b0;
                     			wb_dat_o <= 32'd0;
                     		end else begin
                     			wb_dat_o <= {31'd0, dataBusy};
                     		end
                           	wb_ack_o_buf <= 1'b1;
                       	end
                       	SEND_DATA_OP_POINTER : begin
                       		if(!dataBusy) begin
                     			dataStartFlag <= 1'b1;
                     			dataWriteEnableFlag <= 1'b1;
                     			wb_dat_o <= 32'd0;
                     		end else begin
                     			wb_dat_o <= {31'd0, dataBusy};
                     		end
                           	wb_ack_o_buf <= 1'b1;
                        end
                      	CMD_BUSY_POINTER : begin
                        	wb_dat_o <= {31'b0, commandBusy};
                       		wb_ack_o_buf <= 1'b1;
                     	end 
                     	DATA_BUSY_POINTER : begin
                       		wb_dat_o <= {31'b0, dataBusy};
                      		wb_ack_o_buf <= 1'b1;
                     	end
                      	CMD_STATUS_POINTER : begin
                       		wb_dat_o <= commandStatusVector;
                         	wb_ack_o_buf <= 1'b1;
                     	end 
                      	DATA_STATUS_POINTER : begin
                       		wb_dat_o <= dataStatusVector;
                         	wb_ack_o_buf <= 1'b1;
                    	end
                    	DATA_LENGTH_POINTER : begin
                    		wb_dat_o <= dataLength[10:0];
                    		wb_ack_o_buf <= 1'b1;
                    	end 
                    	default : begin
                       		wb_err_o_buf <= 1'b1;
                    	end
              		endcase
              	end 
            end
        end
    end 


    initial begin
        sd_clock = 1'b0;
    end 

    reg[31:0] clockCounter = 32'd0;

    always @(posedge wb_clk) begin
        if(!wb_rst) begin
            clockCounter <= clockCounter + 1'd1;
            if(clockCounter == (MAIN_CLOCK_FREQUENCY/SD_CLOCK_FREQUENCY)/2-1) begin
                clockCounter <= 32'd0;
                sd_clock <= ~sd_clock;
            end 
        end 
    end


    reg commandStart = 1'b0;
    wire commandFinished;
    wire commandTimeout;
    reg[5:0] commandRequestIndex = 6'd0;
    reg[31:0] commandRequestArgument = 32'd0;
    wire[5:0] commandResponseIndex;
    wire[119:0] commandResponseArgument;

    SDCommandController commandController(
        .reset(wb_rst),
        .sdClock(~sd_clock),
        .sdCommand(sd_command),
    
        .start(commandStart),
        .requestIndex(commandRequestIndex),
        .requestArgument(commandRequestArgument),    
        
        .finished(commandFinished),
        .timeout(commandTimeout),
        .responseIndex(commandResponseIndex),
        .responseArgument(commandResponseArgument)
    );

    reg[31:0] commandStatusVector = 32'b0;
    
    localparam CMD_IDLE = 32'd0;
    localparam CMD_PROCESSING = 32'd1;
    localparam CMD_DONE = 32'd2;
    
    reg[31:0] commandState = CMD_IDLE; 

    always @(negedge sd_clock) begin
        if(!wb_rst) begin
            case(commandState)
                CMD_IDLE : begin
                    commandStatusVector <= 16'd0;
                    if(commandStartFlag) begin
                        commandStart <= 1'b1;
                        commandState <= CMD_PROCESSING;
                    end
                end
                CMD_PROCESSING : begin
                    commandStart <= 1'b0;
                    if(commandFinished) begin
                        commandStatusVector <= {25'b0, commandResponseIndex, commandTimeout}; 
                        commandState <= CMD_DONE;
                    end                    
                end 
                CMD_DONE : begin
                	commandState <= CMD_IDLE;
                end
            endcase
        end
    end

    reg dataStart = 1'b0;
    reg dataWriteEnable = 1'b0;
    wire dataFinished;
    wire dataError;
    wire dataTimeout;
    reg[10:0] dataLength = 11'd512;

    SDDataController dataController(
        .reset(wb_rst),
        .sdClock(~sd_clock),
        .sdData(sd_data),
        
        .write_clock(write_clock),
        .write_enable(write_enable),
        .write_address(write_address),
        .write_data(write_data),
        
        .read_clock(read_clock),
        .read_address(read_address),
        .read_data(read_data),
        
        .start(dataStart),
        .writeEnable(dataWriteEnable), 
        .dataLength(dataLength),

        .finished(dataFinished),
        .error(dataError),    
        .timeout(dataTimeout)
    );

    reg[31:0] dataStatusVector = 16'b0;
    
    localparam DATA_IDLE = 32'd0;
    localparam DATA_PROCESSING = 32'd1;
    localparam DATA_DONE = 32'd2;
    
    reg[31:0] dataState = DATA_IDLE;

    always @(negedge sd_clock) begin
        if(!wb_rst) begin
            case(dataState)
                DATA_IDLE : begin
                    dataStatusVector <= 16'b0;
                    if(dataStartFlag) begin
                        dataStart <= 1'b1;
                        dataWriteEnable <= dataWriteEnableFlag;
                        dataState <= DATA_PROCESSING;
                    end
                end
                DATA_PROCESSING : begin
                    dataStart <= 1'b0;
                    if(dataFinished) begin
                        dataStatusVector <= {30'b0, dataTimeout, dataError}; 
                        dataState <= DATA_DONE;
                    end                    
                end 
                DATA_DONE : begin
                	dataState <= DATA_IDLE;
                end
            endcase
        end
    end



endmodule    
