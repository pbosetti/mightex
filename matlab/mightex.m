classdef mightex < handle
  %MIGHTEX Mightex TCE-1304-U interface
  %   Class that provides a wrapper around the Mightex 1304 library
  
  properties
    Library {mustBeTextScalar} = '../../lib/libmightex.so';
    Header {mustBeTextScalar} = '../../include/mightex1304.h';
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
      %   Optionally, pass path to library and to header file
      if (ispc)
          obj.Library = '../../bin/libmightex.dll';
      elseif(ismac)
          obj.Library = '../../lib/libmightex.dylib';
      end
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
      %delete Delete resources and close connection with device
      if (obj.IsOpen)
        calllib('libmightex', 'mightex_close', obj.Mtx)
      end
      delete(obj.Mtx);
      delete(obj.Frame);
      delete(obj.RawFrame);
      disp("Mightex connection closed. Remember to call 'unloadlibrary libmightex'!");
    end
    
    function cleanBuffer(obj)
      calllib('libmightex', 'mightex_set_mode', obj.Mtx, 0);
    end
    
    function setExposureTime(obj, value)
      %setExposureTime Set exposure time to a value in ms (minimum: 0.1 ms)
      obj.ExposureTime = value;
      calllib('libmightex', 'mightex_set_exptime', obj.Mtx, value);
    end
    
    function m = darkMean(obj)
      %darkMean Return the average of the shielded pixels values
      m = calllib('libmightex', 'mightex_dark_mean', obj.Mtx);
    end
    
    function ts = frameTimestamp(obj)
      %frameTimestamp the timestamp of the last frame
      ts = calllib('libmightex', 'mightex_frame_timestamp', obj.Mtx);
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
    
    function plotFrame(m, thr)
      %plotFrame Plot the last frame readed
      frame = m.Frame.value;
      frameThr = mightex.threshold(frame, thr);
      center = mightex.center(frameThr);
      plot(frame)
      hold on
      plot(frameThr)
      xlabel("Pixel #")
      ylabel("Intensity")
      ylim([0 70000])
      yline(65535, "red")
      xl = xline(center, "green", "Center: " + center);
      xl.Color = "black";
      xl.FontSize = 12;
      hold off
    end
  end
  
  methods (Static)
    function ver = swVersion()
      %swVersion The version of the mightex library
      ver = calllib('libmightex', 'mightex_sw_version');
    end
    
    function thrFrame = threshold(frame, level)
      thrFrame = frame;
      thrFrame(frame < level) = 0;
    end
    
    function center = center(frame)
      num = 0.0;
      den = 0.0;
      frame = double(frame);
      for i = 1:length(frame)
        num = num + i * frame(i);
        den = den + frame(i);
      end
      center = num / den;
    end
  end
end

