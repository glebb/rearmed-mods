# coremltools 9.0 with numpy 2.5: int(np.array([n])) is an error now; make the torch int()/bool() casts of a
# folded one-element array take .item(). Import before converting.
import numpy as np
from coremltools.converters.mil.frontend.torch import ops as _ops
from coremltools.converters.mil import Builder as _mb

_orig_cast = _ops._cast


def _cast(context, node, dtype, dtype_name):
    x = _ops._get_inputs(context, node, expected=1)[0]
    if x.can_be_folded_to_const() and isinstance(x.val, np.ndarray) and x.val.size == 1:
        context.add(_mb.const(val=dtype(x.val.item()), name=node.name), node.name)
        return
    _orig_cast(context, node, dtype, dtype_name)


_ops._cast = _cast
