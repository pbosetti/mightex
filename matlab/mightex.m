classdef mightex < handle
  %MIGHTEX Summary of this class goes here
  %   Detailed explanation goes here
  
  properties
    Library {mustBeTextScalar} = '../products_host/lib/libmightex.so';
    Header {mustBeTextScalar} = '../src/mightex1304.h';
  end
  
  properties (SetAccess = private, GetAccess = public)
    NPixels (1,1) {mustBeInteger} = 0;
    Serial {mustBeTextScalar} = '';
    ExposureTime (1,1) {mustBeNumeric} = 1;
  end
  
  properties (SetAccess = private)
    Frame;
    RawFrame;
    IsOpen = false;
    Mtx;
  end
  
  methods
    function obj = mightex(varargin)
      %MIGHTEX Construct an instance of this class
      %   Detailed explanation goes here
      if (nargin == 1)
        obj.Library = varargin{1};
      elseif (nargin == 2)
        obj.Library = varargin{1};
        obj.Header = varargin{2};
      end
      if (~libisloaded('libmightex'))
        loadlibrary(obj.Library, obj.Header);
      end
      obj.Mtx = calllib('libmightex', 'mightex_new');
      if obj.Mtx.isNull
        calllib('libmightex', 'mightex_close', obj.Mtx)
        disp("Cannot connect to Mightex camera!");
      else
        obj.Serial = calllib('libmightex', 'mightex_serial_no', obj.Mtx);
        obj.IsOpen = true;
        disp("Connected to camera "+obj.Serial);
        calllib('libmightex', 'mightex_set_mode', obj.Mtx, 0); 
        obj.NPixels = calllib('libmightex', 'mightex_pixel_count', obj.Mtx);
        obj.Frame = calllib('libmightex', 'mightex_frame_p', obj.Mtx);
        obj.Frame.setdatatype('uint16Ptr', obj.NPixels, 1);
        obj.RawFrame = calllib('libmightex', 'mightex_raw_frame_p', obj.Mtx);
        obj.RawFrame.setdatatype('uint16Ptr', obj.NPixels, 1);
      end
    end
    
    function close(obj)
      %close Close connection with the camera
      if (obj.IsOpen)
        calllib('libmightex', 'mightex_close', obj.Mtx)
      end
      obj.IsOpen = false;
    end
    
    function delete(obj)
      if (obj.IsOpen)
        calllib('libmightex', 'mightex_close', obj.Mtx)
      end
      delete(obj.Mtx);
      delete(obj.Frame);
      delete(obj.RawFrame);
      disp("Mightex connection closed. Remember to call 'unloadlibrary libmightex'!");
    end
    
    function setExposureTime(obj, value)
      obj.ExposureTime = value;
      calllib('libmightex', 'mightex_set_exptime', obj.Mtx, value);
    end
    
    function [frame, rawFrame, bias] = readFrame(obj)
      %readFrame read the last frame
      %   Read the last frame and return raw values, filtered values, and
      %   dark current average.
      calllib('libmightex', 'mightex_read_frame', obj.Mtx);
      bias = calllib('libmightex', 'mightex_dark_mean', obj.Mtx);
      calllib('libmightex', 'mightex_apply_filter', obj.Mtx, libpointer);
      frame = obj.Frame.value;
      rawFrame = obj.RawFrame.value;
    end
  end
end

