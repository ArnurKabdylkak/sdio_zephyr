`timescale 1ns / 1ps


module SDCommandController(
    input reset,
    input sdClock,
    inout sdCommand,
    
    input start,
    input[5:0] requestIndex,
    input[31:0] requestArgument,    
        
    output reg finished,
    output reg timeout,
    output[5:0] responseIndex,
    output[119:0] responseArgument
    );
    
    localparam CMD_LENGTH = 48;
    localparam LONG_RESPONSE_LENGTH = 8'd136;
    localparam SHORT_RESPONSE_LENGTH = 8'd48;
    localparam TIMEOUT = 1024;

    localparam CMD2_INDEX = 6'd2;
    localparam CMD9_INDEX = 6'd9;
    localparam CMD10_INDEX = 6'd10;
    
    wire[7:0] RESPONSE_LENGTH; 
    assign RESPONSE_LENGTH = (requestIndex == CMD2_INDEX || requestIndex == CMD9_INDEX || requestIndex == CMD10_INDEX) ? LONG_RESPONSE_LENGTH : SHORT_RESPONSE_LENGTH;
    
    reg commandOutputEnable = 1'b1;
    reg commandOut = 1'b1;
    assign sdCommand = commandOutputEnable ? commandOut : 1'bz;
    wire commandIn = commandOutputEnable ? 1'b1 : sdCommand;
    
    function automatic[6:0] CRC7(input[6:0] init, input data);
        if(init[6] ^ data) begin
            CRC7 = {init[5:0], 1'b0} ^ 7'b0001001;    
        end else begin
            CRC7 = {init[5:0], 1'b0};
        end         
    endfunction 
    
    localparam IDLE 			= 32'd0;
    localparam REQUEST 			= 32'd1;
    localparam WAIT_RESPONSE 	= 32'd2;
    localparam RESPONSE 		= 32'd3;
    
    reg[31:0] state = IDLE;
    
    reg[31:0] counter = 32'd0;
    
    reg request_startBit;
    reg request_direction;
    reg[5:0] request_commandIndex;
    reg[31:0] request_argument;
    reg[6:0] request_crc;
    reg request_endBit;
    
    wire[47:0] request_body = {request_startBit, request_direction, request_commandIndex, request_argument, request_crc, request_endBit};
    
    reg response_startBit;
    reg response_direction;
    reg[5:0] response_commandIndexOrReserved;
    reg[119:0] response_argument;
    reg[6:0] response_crc;
    reg response_endBit;
    
    
    
    assign responseArgument = response_argument;
    assign responseIndex = response_commandIndexOrReserved;
    
    always @(posedge sdClock) begin
        if(!reset) begin
            case(state)
                IDLE : begin
                    counter <= 32'd0;

                    response_startBit <= 1'b0;
                    response_direction <= 1'b0;
                    response_commandIndexOrReserved <= 6'd0;
                    response_crc <= 7'd0;
                    response_endBit <= 1'b0;
                                        
                    finished <= 1'b0;
                    timeout <= 1'b0;
                    
                    request_startBit  <= 1'b0;
                    request_direction <= 1'b1;
                    request_crc <= 7'd0;
                    request_endBit <= 1'b1;
                    
                    if(start) begin
                        request_commandIndex <= requestIndex;
                        request_argument <= requestArgument;
                        response_argument <= 120'd0;
                        state <= REQUEST;
                    end 
                end 
                REQUEST : begin
                    counter <= counter + 1'd1;
                    if(counter < 32'd40) begin
                        request_crc <= CRC7(request_crc, request_body[CMD_LENGTH-1-counter]);
                    end                    
                    
                    if(counter == CMD_LENGTH-1) begin
                        counter <= 32'd0;
                        state <= WAIT_RESPONSE;
                    end                                         
                end 
                WAIT_RESPONSE : begin
                    counter <= counter + 1'd1;
                    if(!commandIn) begin
                        response_startBit <= commandIn;
                        counter <= 'd1;
                        state <= RESPONSE;
                    end else if(counter == TIMEOUT) begin
                        counter <= 32'd0;
                        finished <= 1'b1;
                        timeout <= 1'b1; 
                        state <= IDLE;
                    end 
                end
                RESPONSE : begin
                    if(RESPONSE_LENGTH == LONG_RESPONSE_LENGTH) begin
                        if(counter == 32'd1) begin
                        	response_direction <= commandIn;
                        end else if(counter >= 32'd2 && counter <= 32'd7) begin
                        	response_commandIndexOrReserved[5-(counter-2)] <= commandIn;
                        end else if(counter >= 32'd8 && counter <= 32'd127) begin
                        	response_argument[119-(counter-8)] <= commandIn;
                        end else if(counter >= 32'd128 && counter <= 32'd134) begin
                        	response_crc[6-(counter-128)] <= commandIn;
                        end else if(counter == 32'd135) begin
                        	response_endBit <= commandIn;
                        end 
                    end else begin
                        if(counter == 32'd1) begin
                        	response_direction <= commandIn;
                        end else if(counter >= 32'd2 && counter <= 32'd7) begin
                        	response_commandIndexOrReserved[5-(counter-2)] <= commandIn;
                        end else if(counter >= 32'd8 && counter <= 32'd39) begin
                        	response_argument[31-(counter-8)] <= commandIn;
                        end else if(counter >= 32'd40 && counter <= 32'd46) begin
                        	response_crc[6-(counter-40)] <= commandIn;
                        end else if(counter == 32'd47) begin
                        	response_endBit <= commandIn;
                        end  
                    end 
                    counter <= counter + 1'd1;
                    if(counter == RESPONSE_LENGTH-1) begin    
                        counter <= 32'd0; 
                        finished <= 1'b1;
                        state <= IDLE;
                    end 
                end                
            endcase
        
        end else begin
        end 
    end 
    
    always @(*) begin
        case(state)
            IDLE : begin
                commandOutputEnable = 1'b1;
                commandOut = 1'b1;
            end 
            REQUEST : begin
                commandOutputEnable = 1'b1;
                commandOut = request_body[CMD_LENGTH-1-counter];
            end 
            WAIT_RESPONSE : begin
                commandOutputEnable = 1'b0;
                commandOut = 1'b1;
            end 
            RESPONSE : begin
                commandOutputEnable = 1'b0;
                commandOut = 1'b1;
            end 
            default : begin
                commandOutputEnable = 1'b1;
                commandOut = 1'b1;
            end 
        endcase 
    end 
endmodule
