classdef InheritCheckMx < PyMxRaw
    methods
        function obj = InheritCheckMx()
            mod = pyimport_proxy('inherit_check_py');
            obj@PyMxRaw(mod.PyMxExtend);
        end

        function out = pure(~, value)
            out = value;
            fprintf('ml.pure=%s\n', value);
        end
        function out = optional(~, value)
            out = 1000 * value;
            fprintf('ml.optional=%s\n', value);
        end

        % How to handle non-virtual methods?
        function out = dispatch(obj, value)
            out = obj.pyInvokeDirect('dispatch', value);
        end
    end
end
