module mb_de0nano(
	input          CLOCK_50,
	output [  7:0] LED,
	input  [  1:0] KEY,
	input  [  3:0] SW,

	output         MAX_RST,
	output         MAX_SS,
	output         MAX_SCLK,
	output         MAX_MOSI,
	input          MAX_MISO,
	input          MAX_INT,
	input          MAX_GPX
);

	reg          rst_n;

	wire         clk;
	wire         w_rst_n;
	wire         w_max_ss;
	wire         w_max_sclk;
	wire         w_max_mosi;
	wire         w_max_miso;
	wire [  7:0] w_pio_0;

	assign clk = CLOCK_50;
	assign w_rst_n = KEY[0];
	assign w_max_miso = MAX_MISO;
	assign w_pio_0[0] = MAX_INT;
	assign w_pio_0[1] = MAX_GPX;
	assign w_pio_0[7:2] = 6'd0;

	always @( posedge clk or negedge w_rst_n ) begin
		if( ! w_rst_n ) begin
			rst_n <= 1'b0;
		end else begin
			rst_n <= 1'b1;
		end
	end

	mb_de0nano_sopc mb_de0nano_sopc(
		.MISO_to_the_spi_0    ( w_max_miso ),
		.MOSI_from_the_spi_0  ( w_max_mosi ),
		.SCLK_from_the_spi_0  ( w_max_sclk ),
		.SS_n_from_the_spi_0  ( w_max_ss ),
		.clk_0                ( clk ),
		.in_port_to_the_pio_0 ( w_pio_0 ),
		.reset_n              ( rst_n )
	);

	assign LED = 8'b0000_0000;
	assign MAX_RST  = rst_n;
	assign MAX_SS   = w_max_ss;
	assign MAX_SCLK = w_max_sclk;
	assign MAX_MOSI = w_max_mosi;

endmodule

