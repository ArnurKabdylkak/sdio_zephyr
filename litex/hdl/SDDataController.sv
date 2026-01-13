

module SDDataController(
    input reset,
    input sdClock,
    inout[3:0] sdData,
    
    input write_clock,
    input write_enable,
    input[8:0] write_address,
    input[31:0] write_data,
    
    input read_clock,
    input[8:0] read_address,
    output reg[31:0] read_data,
        
    input start,
    input writeEnable,
    input[$clog2(2048)-1:0] dataLength, // in bytes
    output reg finished,
    output reg error,    
    output reg timeout
);
    localparam TIMEOUT = 1024;
    localparam CRC16_WIDTH = 16;

    wire[3:0] sdDataIn;
    reg[3:0] sdDataOut = 4'b1111;
    reg sdDataOutputEnable = 1'b1;
    assign sdData = sdDataOutputEnable ? sdDataOut : 4'bzzzz;
    assign sdDataIn = sdDataOutputEnable ? 4'b1111 : sdData;
    
    
    localparam IDLE 					= 32'd0;
    localparam READ_START_BIT 			= 32'd1;
    localparam READ_DATA 				= 32'd2;
    localparam READ_CRC 				= 32'd3;
    localparam READ_END_BIT 			= 32'd4;
    localparam SEND_START_BIT 			= 32'd5;
    localparam SEND_DATA 				= 32'd6;
    localparam SEND_CRC 				= 32'd7;
    localparam SEND_END_BIT 			= 32'd8;
    localparam RESPONSE_TOKEN_WAIT 		= 32'd9;
    localparam RESPONSE_TOKEN_READ 		= 32'd10;
    localparam RESPONSE_TOKEN_FINISH 	= 32'd11;
    localparam WAIT4FREE 				= 32'd12;
    
    reg[31:0] state = IDLE;

    reg[31:0] counter = 32'b0;        
    reg[CRC16_WIDTH-1:0] readCRC[3:0];
    reg[CRC16_WIDTH-1:0] readCRCCalculated[3:0];
    reg[CRC16_WIDTH-1:0] writeCRC[3:0];    
    
    //----------------WRITE BUFFER-------------------//
    reg[31:0] writeBuffer[511:0];
    
    always @(posedge write_clock) begin
        if(!reset) begin
            if(write_enable)
                writeBuffer[write_address] <= write_data;                 
        end 
    end
    
    reg[31:0] writeDataBuffer = 32'd0;
    wire[8:0] writeDataBufferReadAddress = ((counter+3'd4)/32);
    
    always @(posedge sdClock) begin
        if(!reset) begin
            writeDataBuffer <= writeBuffer[writeDataBufferReadAddress];                      
        end 
    end 
   
    //---------------READ BUFFER------------------------//    
    reg[31:0] readBuffer[511:0];
    
    always @(posedge read_clock) begin
        if(!reset)
            read_data <= readBuffer[read_address];
    end 
    
    reg[31:0] readDataBuffer = 32'b0;
    
    always @(posedge sdClock) begin
        if(!reset) begin
            if(state == READ_DATA) begin
                readDataBuffer[31-(counter%32)-:4] <= sdDataIn;
                if(counter%32 == 28) begin
                    readBuffer[counter/32] <= {readDataBuffer[31-:28], sdDataIn};
                end           
            end 
        end 
    end    

/*
    // MOCK write data
    
    initial begin
        for(integer i = 0; i < 512; i = i + 1) begin
            writeBuffer[i] <= 32'h0e0e0e0e;
        end
    end 
  */  
    reg[2:0] responseToken = 3'b0;
    integer i = 0;
    
    always @(posedge sdClock) begin
        if(!reset) begin
            case(state)
                IDLE : begin
                    counter <= 32'd0;
                    finished <= 1'b0;
                    error <= 1'b0;
                    timeout <= 1'b0;
                    for(i = 0; i < 4; i = i + 1) begin
                        readCRCCalculated[i] <= 16'd0;
                        writeCRC[i] <= 16'd0;
                    end
                    if(start && sdData[0]) begin
                        if(writeEnable) begin
                            state <= SEND_START_BIT;
                        end else begin
                            state <= READ_START_BIT;
                        end 
                    end 
                end 
                READ_START_BIT : begin
                    counter <= counter + 1'd1;
                    if(!sdData[0]) begin
                        counter <= 32'd0;
                        state <= READ_DATA;
                    end else if(counter == TIMEOUT) begin
                        counter <= 32'd0;
                        finished <= 1'b1;
                        timeout <= 1'b1;
                        state <= IDLE;
                    end
                end 
                READ_DATA : begin
                    for(i = 0; i < 4; i = i + 1) begin
                        readCRCCalculated[i] <= CRC16(readCRCCalculated[i], sdDataIn[i]);
                    end
                    counter <= counter + 4;
                    if(counter == (8*dataLength)-4) begin
                        counter <= 32'd0;
                        state <= READ_CRC;                           
                    end   
                end
                READ_CRC : begin
                    for(i = 0; i < 4; i = i + 1) begin
                        readCRC[i][CRC16_WIDTH-1-counter] <= sdDataIn[i];
                    end
                    counter <= counter + 1'd1;
                    if(counter == CRC16_WIDTH-1) begin
                        counter <= 32'd0;
                        state <= READ_END_BIT;
                    end 
                end 
                READ_END_BIT : begin
                    for(i = 0; i < 4; i = i + 1) begin
                        if(readCRC[i] != readCRCCalculated[i]) begin
                            error <= 1'b1;
                        end
                    end
                    finished <= 1'b1;
                    state <= IDLE;
                end 
                SEND_START_BIT : begin
                    state <= SEND_DATA;
                end                 
                SEND_DATA : begin
                    for(i = 0; i < 4; i = i + 1) begin
                        writeCRC[i] <= CRC16(writeCRC[i], sdDataOut[i]);
                    end                    
                    counter <= counter + 3'd4;
                    if(counter == (8*dataLength)-4) begin
                        counter <= 1'b0;
                        state <= SEND_CRC;                           
                    end
                end
                SEND_CRC : begin
                    counter <= counter + 1'd1;
                    if(counter == CRC16_WIDTH-1) begin
                        counter <= 32'd0;
                        state <= SEND_END_BIT;
                    end
                end 
                SEND_END_BIT : begin
                    state <= RESPONSE_TOKEN_WAIT;                    
                end 
                RESPONSE_TOKEN_WAIT : begin
                    counter <= counter + 1'd1;
                    if(!sdDataIn[0]) begin
                        counter <= 32'd0;
                        state <= RESPONSE_TOKEN_READ;
                    end else if(counter == TIMEOUT) begin
                        counter <= 32'd0;
                        finished <= 1'b1;
                        error <= 1'b1;
                        timeout <= 1'b1;
                        state <= IDLE;
                    end
                end
                RESPONSE_TOKEN_READ : begin
                    counter <= counter + 'd1;
                    responseToken[2-counter] <= sdDataIn[0];
                    if(counter == 32'd2) begin
                        counter <= 32'd0;
                        state <= RESPONSE_TOKEN_FINISH;
                    end
                end
                RESPONSE_TOKEN_FINISH : begin
                    state <= WAIT4FREE;
                end
                WAIT4FREE : begin
                    if(sdData[0]) begin // todo: add timeout control
                        finished <= 1'b1;
                        error <= responseToken != 3'b010;                        
                        state <= IDLE;
                    end
                end 
            endcase
        end 
    end 
    
    always @(*) begin
        sdDataOut = 4'b1111;
        case(state)
            IDLE : begin
                sdDataOutputEnable = 1'b1;  
            end 
            READ_START_BIT : begin
                sdDataOutputEnable = 1'b0;
            end 
            READ_DATA : begin
                sdDataOutputEnable = 1'b0;
            end 
            READ_CRC : begin
                sdDataOutputEnable = 1'b0;
            end 
            READ_END_BIT : begin
                sdDataOutputEnable = 1'b0;
            end 
            SEND_START_BIT : begin
                sdDataOutputEnable = 1'b1;
                sdDataOut = 4'b0000;
            end 
            SEND_DATA : begin
                sdDataOutputEnable = 1'b1;
                sdDataOut = writeDataBuffer[31-(counter%32)-:4];
            end 
            SEND_CRC : begin
                sdDataOutputEnable = 1'b1;
                for(i = 0; i < 4; i = i + 1) begin
                    sdDataOut[i] = writeCRC[i][CRC16_WIDTH-1-counter];
                end
            end 
            SEND_END_BIT : begin
                sdDataOutputEnable = 1'b1;
                sdDataOut = 4'b1111;
            end 
            RESPONSE_TOKEN_WAIT, RESPONSE_TOKEN_READ, RESPONSE_TOKEN_FINISH, WAIT4FREE : begin
                sdDataOutputEnable = 1'b0;
            end
            default : begin
                sdDataOutputEnable = 1'b1;
            end 
        endcase
    end 
    
    function automatic[15:0] CRC16(input[15:0] init, input data);
        if(init[15] ^ data) begin
            CRC16 = {init[14:0], 1'b0} ^ 16'b0001000000100001;    
        end else begin
            CRC16 = {init[14:0], 1'b0};
        end
    endfunction    
    
    
    
   
    
endmodule
