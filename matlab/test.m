%% Load library
[missing, warn] = loadlibrary('../products_host/lib/libmightex.so', '../src/mightex1304.h');
libfunctionsview libmightex
mtx = calllib('libmightex', 'mightex_new');

if mtx.isNull
  calllib('libmightex', 'mightex_close', mtx)
  clear mtx
  unloadlibrary libmightex
end
  
%% read serial number
calllib('libmightex', 'mightex_serial_no', mtx);

%% prepare for reading data
calllib('libmightex', 'mightex_set_exptime', mtx, 0.1); % 0.1 ms
calllib('libmightex', 'mightex_set_mode', mtx, 0);      % normal mode
% get data pointer
f = calllib('libmightex', 'mightex_frame_p', mtx);
% get number of pixels
npx = calllib('libmightex', 'mightex_pixel_count', mtx);
f.setdatatype('uint16Ptr', npx, 1);

%% read a single frame
calllib('libmightex', 'mightex_read_frame', mtx);
bias = calllib('libmightex', 'mightex_dark_mean', mtx);
data = f.value;

%% close connection and library
calllib('libmightex', 'mightex_close', mtx)
% first, remove all handles
clear f mtx
% then, close connection and unload library
unloadlibrary libmightex